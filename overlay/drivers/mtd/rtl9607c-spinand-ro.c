// SPDX-License-Identifier: GPL-2.0-only
/* RTL9607C stock-compatible SPI-NAND reader.
 * Register protocol and BCH layout follow AX10v3_GPL's luna_mtd_nand.
 * Deliberately no write-enable, program, erase, markbad or on-flash BBT.
 * Physical offsets are preserved; this does NOT implement vendor skip-BBT.
 */
#include <linux/bitops.h>
#include <linux/delay.h>
#include <linux/dma-mapping.h>
#include <linux/io.h>
#include <linux/iopoll.h>
#include <linux/module.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/partitions.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/sizes.h>

#define PAGE_BYTES 2048
#define OOB_BYTES 64
#define BLOCK_BYTES (PAGE_BYTES * 64)
#define CHIP_BYTES SZ_128M
#define SCRATCH_OFF 2176 /* cacheline-aligned six tag + ten BCH bytes */
#define BUF_BYTES 4096
#define SN_CFG 0x00
#define SN_CS 0x04
#define SN_WCMD 0x08
#define SN_RCMD 0x0c
#define SN_RDATA 0x10
#define SN_WDATA 0x14
#define SN_STATUS 0x40
#define ECC_CFG 0x00
#define ECC_TRIG 0x08
#define ECC_DATA 0x0c
#define ECC_TAG 0x10
#define ECC_STATUS 0x14
#define ECC_IRQ 0x18

struct phoebus_nand {
	struct device *dev;
	void __iomem *sn, *ecc;
	struct mutex lock;
	struct mtd_info mtd;
	u8 *buf;
	dma_addr_t dma;
	u64 pages, raw_pages, corrected, failed, timeouts, bad_queries, marker_reads;
	u64 bch_sectors;
	u32 last_page, last_ecc, last_ecc_page;
	bool poisoned;
};

static const struct mtd_partition stock_parts[] = {
	{ .name = "boot", .offset = 0, .size = SZ_1M, .mask_flags = MTD_WRITEABLE },
	{ .name = "env", .offset = SZ_1M, .size = SZ_1M, .mask_flags = MTD_WRITEABLE },
	{ .name = "ubi_device", .offset = 0x200000, .size = 0x3300000, .mask_flags = MTD_WRITEABLE },
	{ .name = "ubi_device_1", .offset = 0x3500000, .size = 0x3300000, .mask_flags = MTD_WRITEABLE },
	{ .name = "userconfig", .offset = 0x6800000, .size = SZ_8M, .mask_flags = MTD_WRITEABLE },
	{ .name = "tp_data", .offset = 0x7000000, .size = SZ_8M, .mask_flags = MTD_WRITEABLE },
	{ .name = "paniclog", .offset = 0x7800000, .size = SZ_4M, .mask_flags = MTD_WRITEABLE },
	{ .name = "defaults", .offset = 0x7c00000, .size = SZ_4M, .mask_flags = MTD_WRITEABLE },
};

/* The GPL driver uses native-endian volatile MMIO, not little-endian readl. */
static u32 rd(void __iomem *base, unsigned int reg)
{
	return __raw_readl(base + reg);
}

static void wr(void __iomem *base, unsigned int reg, u32 value)
{
	__raw_writel(value, base + reg);
	/* Order native-endian controller writes before polling or DMA kickoff. */
	wmb();
}

static int sn_wait(struct phoebus_nand *n)
{
	u32 v;
	int ret = read_poll_timeout(rd, v, !(v & BIT(3)), 1, 2000,
				    false, n->sn, SN_STATUS);
	if (ret) {
		n->timeouts++;
		n->poisoned = true;
		dev_err_ratelimited(n->dev, "controller timeout; reads disabled\n");
	}
	return ret;
}

static int sn_tx(struct phoebus_nand *n, u32 word, unsigned int len)
{
	int ret = sn_wait(n);

	if (ret)
		return ret;
	wr(n->sn, SN_WCMD, len - 1);
	wr(n->sn, SN_WDATA, word);
	return sn_wait(n);
}

static int sn_rx(struct phoebus_nand *n, u8 *buf, unsigned int len)
{
	unsigned int i, chunk;
	u32 word;
	int ret;

	while (len) {
		chunk = min(len, 4U);
		wr(n->sn, SN_RCMD, chunk - 1);
		ret = sn_wait(n);
		if (ret)
			return ret;
		word = rd(n->sn, SN_RDATA);
		for (i = 0; i < chunk; i++)
			buf[i] = word >> (24 - i * 8);
		buf += chunk;
		len -= chunk;
	}
	return 0;
}

static int sn_feature(struct phoebus_nand *n, u8 addr, u8 *val)
{
	int ret;

	wr(n->sn, SN_CS, 0);
	ret = sn_tx(n, (0x0fU << 24) | (addr << 16), 2);
	if (!ret)
		ret = sn_rx(n, val, 1);
	wr(n->sn, SN_CS, 1);
	return ret;
}

static int sn_load_page(struct phoebus_nand *n, u32 page)
{
	u8 status;
	unsigned int tries;
	int ret;

	if (n->poisoned)
		return -EIO;
	n->last_page = page;
	wr(n->sn, SN_CS, 0);
	ret = sn_tx(n, (0x13U << 24) | page, 4);
	wr(n->sn, SN_CS, 1);
	if (ret)
		return ret;
	for (tries = 0; tries < 200; tries++) {
		ret = sn_feature(n, 0xc0, &status);
		if (ret)
			return ret;
		if (!(status & BIT(0)))
			break;
		usleep_range(50, 100);
	}
	if (tries == 200) {
		n->timeouts++;
		n->poisoned = true;
		return -ETIMEDOUT;
	}
	return 0;
}

static int sn_cache_read(struct phoebus_nand *n, unsigned int column,
			 u8 *buf, unsigned int len)
{
	int ret;

	wr(n->sn, SN_CS, 0);
	/* 0x03, two column bytes, one dummy byte; 1-1-1 mode. */
	ret = sn_tx(n, 0x03000000 | (column << 8), 4);
	if (!ret)
		ret = sn_rx(n, buf, len);
	wr(n->sn, SN_CS, 1);
	return ret;
}

static int sn_page(struct phoebus_nand *n, u32 page)
{
	int ret = sn_load_page(n, page);

	if (!ret)
		ret = sn_cache_read(n, 0, n->buf, PAGE_BYTES + OOB_BYTES);
	if (!ret)
		n->pages++;
	return ret;
}

static int erased_sector(const u8 *data, const u8 *tag)
{
	unsigned int i, flips = 0;

	for (i = 0; i < 512; i++)
		flips += hweight8(~data[i]);
	for (i = 0; i < 16; i++)
		flips += hweight8(~tag[i]);
	return flips <= 4 ? flips : -1; /* stock MAX_ALLOWED_ERR_IN_BLANK_PAGE */
}

static int bch_decode(struct phoebus_nand *n)
{
	unsigned int sector, bits, max_bits = 0;
	u8 *tag = n->buf + SCRATCH_OFF;
	u32 cfg, status;
	int ret;
	bool ecc_error = false;

	for (sector = 0; sector < 4; sector++) {
		memcpy(tag, n->buf + PAGE_BYTES + sector * 6, 6);
		memcpy(tag + 6, n->buf + PAGE_BYTES + 24 + sector * 10, 10);
		/* Inspect erased data BEFORE the decoder can modify it. */
		ret = erased_sector(n->buf + sector * 512, tag);
		if (ret >= 0) {
			memset(n->buf + sector * 512, 0xff, 512);
			memset(n->buf + PAGE_BYTES + sector * 6, 0xff, 6);
			memset(n->buf + PAGE_BYTES + 24 + sector * 10, 0xff, 10);
			n->mtd.ecc_stats.corrected += ret;
			n->corrected += ret;
			max_bits = max_t(unsigned int, max_bits, ret);
			continue;
		}
		cfg = rd(n->ecc, ECC_CFG);
		cfg = (cfg & ~GENMASK(29, 28)) | BIT(20); /* BCH6, dummy-ready */
		wr(n->ecc, ECC_CFG, cfg);
		wr(n->ecc, ECC_IRQ, BIT(0));
		wr(n->ecc, ECC_TAG, n->dma + SCRATCH_OFF);
		wr(n->ecc, ECC_DATA, n->dma + sector * 512);
		dma_wmb();
		n->bch_sectors++;
		n->last_ecc_page = n->last_page;
		wr(n->ecc, ECC_TRIG, 0); /* decode, not encode */
		ret = read_poll_timeout(rd, status, !(status & 3), 1, 2000,
					false, n->ecc, ECC_STATUS);
		n->last_ecc = status;
		if (ret) {
			n->timeouts++;
			n->poisoned = true;
			dev_err_ratelimited(n->dev, "BCH timeout; DMA buffer retained, reads disabled\n");
			return ret;
		}
		dma_rmb();
		if (status & BIT(8)) {
			n->mtd.ecc_stats.failed++;
			n->failed++;
			dev_warn_ratelimited(n->dev, "uncorrectable BCH page=%u sector=%u status=%08x\n",
					    n->last_page, sector, status);
			ecc_error = true;
			continue;
		}
		bits = (status >> 12) & 0xff;
		if (bits > 6) {
			n->mtd.ecc_stats.failed++;
			n->failed++;
			ecc_error = true;
			continue;
		}
		n->mtd.ecc_stats.corrected += bits;
		n->corrected += bits;
		max_bits = max(max_bits, bits);
		memcpy(n->buf + PAGE_BYTES + sector * 6, tag, 6);
	}
	return ecc_error ? -EBADMSG : (int)max_bits;
}

static int nand_read_oob(struct mtd_info *mtd, loff_t from, struct mtd_oob_ops *ops)
{
	struct phoebus_nand *n = mtd->priv;
	size_t data_left = ops->len, oob_left = ops->ooblen, chunk;
	unsigned int off = from % PAGE_BYTES, oob_off = ops->ooboffs;
	unsigned int avail = ops->mode == MTD_OPS_AUTO_OOB ? 20 : OOB_BYTES;
	u32 page = from / PAGE_BYTES;
	int ret = 0, max_bits = 0;
	bool ecc_error = false;

	if (ops->mode != MTD_OPS_RAW && ops->mode != MTD_OPS_PLACE_OOB &&
	    ops->mode != MTD_OPS_AUTO_OOB)
		return -EINVAL;
	if (oob_left && oob_off >= avail)
		return -EINVAL;
	ops->retlen = ops->oobretlen = 0;
	mutex_lock(&n->lock);
	while (data_left || oob_left) {
		if (page >= CHIP_BYTES / PAGE_BYTES) {
			ret = -EINVAL;
			break;
		}
		ret = sn_page(n, page++);
		if (!ret && ops->mode != MTD_OPS_RAW)
			ret = bch_decode(n);
		else if (!ret)
			n->raw_pages++;
		if (ret == -EBADMSG) {
			ecc_error = true;
			ret = 0;
		}
		if (ret < 0)
			break;
		max_bits = max(max_bits, ret);
		chunk = min_t(size_t, data_left, PAGE_BYTES - off);
		if (chunk) {
			memcpy(ops->datbuf + ops->retlen, n->buf + off, chunk);
			ops->retlen += chunk;
			data_left -= chunk;
		}
		chunk = min_t(size_t, oob_left, avail - oob_off);
		if (chunk) {
			memcpy(ops->oobbuf + ops->oobretlen,
			       n->buf + PAGE_BYTES + oob_off +
			       (ops->mode == MTD_OPS_AUTO_OOB ? 2 : 0), chunk);
			ops->oobretlen += chunk;
			oob_left -= chunk;
		}
		off = oob_off = 0;
		cond_resched();
	}
	mutex_unlock(&n->lock);
	return ret < 0 ? ret : ecc_error ? -EBADMSG : max_bits;
}

static int nand_erase_denied(struct mtd_info *mtd, struct erase_info *instr)
{
	/* MTD requires an erase callback for erase-capable media. This callback
	 * satisfies registration without ever touching the controller.
	 */
	return -EROFS;
}

static int nand_isbad(struct mtd_info *mtd, loff_t pos)
{
	struct phoebus_nand *n = mtd->priv;
	u32 page = (pos / BLOCK_BYTES) * 64;
	int ret;

	mutex_lock(&n->lock);
	n->bad_queries++;
	/* MTD partition registration asks about every eraseblock. Read only
	 * the marker for the usual good block, avoiding a slow full-page sweep.
	 */
	ret = sn_load_page(n, page);
	if (!ret)
		ret = sn_cache_read(n, PAGE_BYTES, n->buf + PAGE_BYTES, 1);
	if (!ret)
		n->marker_reads++;
	/* Stock uses the first page's first tag byte, with BCH correction. */
	if (!ret && n->buf[PAGE_BYTES] != 0xff) {
		ret = sn_page(n, page);
		if (!ret)
			ret = bch_decode(n);
		if (ret == -EBADMSG) {
			/* An undecodable marker is conservatively bad, never good. */
			ret = 1;
			goto out;
		}
	}
	if (ret >= 0)
		ret = n->buf[PAGE_BYTES] != 0xff;
out:
	if (ret == 1)
		dev_warn_ratelimited(n->dev, "bad/suspect marker block=%u\n", page / 64);
	mutex_unlock(&n->lock);
	return ret;
}

static int oob_free(struct mtd_info *mtd, int section, struct mtd_oob_region *r)
{
	if (section)
		return -ERANGE;
	r->offset = 2;
	r->length = 20;
	return 0;
}

static int oob_ecc(struct mtd_info *mtd, int section, struct mtd_oob_region *r)
{
	if (section)
		return -ERANGE;
	r->offset = 24;
	r->length = 40;
	return 0;
}

static const struct mtd_ooblayout_ops oob_ops = { .free = oob_free, .ecc = oob_ecc };

static ssize_t telemetry_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct phoebus_nand *n = dev_get_drvdata(dev);
	ssize_t len;

	mutex_lock(&n->lock);
	len = sysfs_emit(buf, "readonly=1 physical_offsets=1 skip_bbt=0 pages=%llu raw_pages=%llu bch_sectors=%llu corrected=%llu failed=%llu timeouts=%llu bad_queries=%llu marker_reads=%llu poisoned=%u last_page=%u last_ecc_page=%u last_ecc=%08x\n",
			n->pages, n->raw_pages, n->bch_sectors, n->corrected, n->failed, n->timeouts,
			n->bad_queries, n->marker_reads, n->poisoned, n->last_page,
			n->last_ecc_page, n->last_ecc);
	mutex_unlock(&n->lock);
	return len;
}
static DEVICE_ATTR_RO(telemetry);

static int phoebus_nand_probe(struct platform_device *pdev)
{
	struct phoebus_nand *n;
	u8 id[3], config;
	int ret;

	n = devm_kzalloc(&pdev->dev, sizeof(*n), GFP_KERNEL);
	if (!n)
		return -ENOMEM;
	n->dev = &pdev->dev;
	n->sn = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(n->sn))
		return PTR_ERR(n->sn);
	n->ecc = devm_platform_ioremap_resource(pdev, 1);
	if (IS_ERR(n->ecc))
		return PTR_ERR(n->ecc);
	mutex_init(&n->lock);
	/* Retain the bootloader's tested divider, byte order and bus timings. */
	wr(n->sn, SN_CFG, rd(n->sn, SN_CFG) & ~BIT(20));
	wr(n->sn, SN_CS, 0);
	ret = sn_tx(n, 0x9f000000, 2); /* READ_ID + dummy */
	if (!ret)
		ret = sn_rx(n, id, sizeof(id));
	wr(n->sn, SN_CS, 1);
	if (ret)
		return ret;
	dev_info(n->dev, "ID=%02x:%02x:%02x SN_CFG=%08x ECC_CFG=%08x\n",
		 id[0], id[1], id[2], rd(n->sn, SN_CFG), rd(n->ecc, ECC_CFG));
	if (id[0] != 0xc8 || id[1] != 0x01)
		return dev_err_probe(n->dev, -ENODEV, "only stock ESMT F50L1G41LB (c8:01) supported\n");
	ret = sn_feature(n, 0xb0, &config);
	if (ret)
		return ret;
	if (config & BIT(4))
		return dev_err_probe(n->dev, -EOPNOTSUPP, "on-die ECC enabled; refusing incompatible stock-layout reads\n");
	ret = dma_set_mask_and_coherent(n->dev, DMA_BIT_MASK(32));
	if (ret)
		return ret;
	n->buf = dma_alloc_coherent(n->dev, BUF_BYTES, &n->dma, GFP_KERNEL);
	if (!n->buf)
		return -ENOMEM;
	n->mtd = (struct mtd_info) {
		.name = "spinand", .type = MTD_NANDFLASH,
		.flags = MTD_CAP_NANDFLASH & ~MTD_WRITEABLE,
		.size = CHIP_BYTES, .erasesize = BLOCK_BYTES,
		.writesize = PAGE_BYTES, .writebufsize = PAGE_BYTES,
		.oobsize = OOB_BYTES, .oobavail = 20,
		.ecc_strength = 6, .ecc_step_size = 512,
		.bitflip_threshold = 4, .owner = THIS_MODULE, .priv = n,
		._erase = nand_erase_denied, ._read_oob = nand_read_oob,
		._block_isbad = nand_isbad,
	};
	n->mtd.dev.parent = n->dev;
	mtd_set_ooblayout(&n->mtd, &oob_ops);
	platform_set_drvdata(pdev, n);
	/* No cmdline parser: even a malicious mtdparts cannot enable writes. */
	ret = mtd_device_register(&n->mtd, stock_parts, ARRAY_SIZE(stock_parts));
	if (ret) {
		/* Registration scans markers and can start BCH DMA. A timeout
		 * must retain this buffer even when the probe itself fails.
		 */
		if (!n->poisoned)
			dma_free_coherent(n->dev, BUF_BYTES, n->buf, n->dma);
		else
			dev_err(n->dev, "registration failed after timeout; DMA buffer retained\n");
		return ret;
	}
	ret = device_create_file(n->dev, &dev_attr_telemetry);
	if (ret)
		dev_warn(n->dev, "telemetry unavailable: %d\n", ret);
	dev_info(n->dev, "128 MiB physical MTD, stock BCH6/tag layout; ALL partitions read-only\n");
	return 0;
}

static const struct of_device_id phoebus_nand_match[] = {
	{ .compatible = "realtek,rtl9607c-spinand-ro" }, {}
};

/* Built-in, no unbind: a timed-out BCH engine must never DMA into freed RAM. */
static struct platform_driver phoebus_nand_driver = {
	.probe = phoebus_nand_probe,
	.driver = { .name = "phoebus-spinand-ro", .of_match_table = phoebus_nand_match,
		    .suppress_bind_attrs = true },
};
builtin_platform_driver(phoebus_nand_driver);
