// SPDX-License-Identifier: MIT
/* Synthetic-password benchmark only. Does not read or alter credentials. */
#define _GNU_SOURCE
#include <argon2.h>
#include <crypt.h>
#include <openssl/evp.h>
#include <openssl/crypto.h>
#include <errno.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <linux/if_alg.h>
#include <time.h>
#include <unistd.h>

#define MAX_CPUS 4
static const char password[] = "synthetic-benchmark-password-only";
static const unsigned char salt[] = "benchmark-salt!!";
#define SALT_LEN (sizeof(salt)-1)
static const char *algorithm;
static unsigned work, memory_kib, lanes, jobs;

static double seconds(void)
{
	struct timespec t;
	if (clock_gettime(CLOCK_MONOTONIC, &t)) exit(2);
	return t.tv_sec + t.tv_nsec / 1e9;
}

static long read_memory(const char *path, const char *field)
{
	FILE *f = fopen(path, "r");
	char line[256], key[80];
	long value, result = -1;
	if (!f) return -1;
	while (fgets(line, sizeof(line), f)) {
		if (sscanf(line, "%79s %ld", key, &value) == 2 && !strcmp(key, field)) {
			result = value; break;
		}
	}
	fclose(f);
	return result;
}

static long temperature(void)
{
	FILE *f = fopen("/sys/class/hwmon/hwmon0/temp1_input", "r");
	long n = -1;
	if (f) { if (fscanf(f, "%ld", &n) != 1) n = -1; fclose(f); }
	return n;
}

struct monitor {
	atomic_int done;
	long min_available, peak_rss, max_temp;
};

static void *monitor(void *arg)
{
	struct monitor *m = arg;
	do {
		long available = read_memory("/proc/meminfo", "MemAvailable:");
		long rss = read_memory("/proc/self/status", "VmRSS:");
		long temp = temperature();
		if (available >= 0 && available < m->min_available) m->min_available = available;
		if (rss > m->peak_rss) m->peak_rss = rss;
		if (temp > m->max_temp) m->max_temp = temp;
		if (available >= 0 && available < 24 * 1024) {
			static const char msg[] = "ABORT: memory reserve below 24 MiB\n";
			(void)write(2, msg, sizeof(msg)-1); _exit(77);
		}
		if (temp >= 78000) {
			static const char msg[] = "ABORT: conservative benchmark temperature guard 78 C\n";
			(void)write(2, msg, sizeof(msg)-1); _exit(78);
		}
		struct timespec delay = {.tv_nsec = 250000000};
		nanosleep(&delay, NULL);
	} while (!atomic_load(&m->done));
	return NULL;
}

struct cpu_sample { unsigned long long total, idle; };
static void cpus(struct cpu_sample sample[MAX_CPUS+1])
{
	FILE *f = fopen("/proc/stat", "r");
	char line[256], name[24];
	int row = 0;
	memset(sample, 0, sizeof(*sample) * (MAX_CPUS+1));
	if (!f) return;
	while (row <= MAX_CPUS && fgets(line, sizeof(line), f)) {
		unsigned long long n[8] = {0};
		if (sscanf(line, "%23s %llu %llu %llu %llu %llu %llu %llu %llu", name,
			&n[0], &n[1], &n[2], &n[3], &n[4], &n[5], &n[6], &n[7]) != 9) break;
		if (strncmp(name, "cpu", 3)) break;
		for (int i=0; i<8; i++) sample[row].total += n[i];
		sample[row++].idle = n[3] + n[4];
	}
	fclose(f);
}

static double utilization(struct cpu_sample a, struct cpu_sample b)
{
	unsigned long long total = b.total-a.total, idle = b.idle-a.idle;
	return total ? 100.0 * (total-idle) / total : 0;
}

struct job { int error; double wall; unsigned char result[32]; };
static void *hash_job(void *arg)
{
	struct job *j = arg;
	double start = seconds();
	if (!strcmp(algorithm, "argon2id")) {
		j->error = argon2id_hash_raw(work, memory_kib, lanes, password, strlen(password),
			salt, SALT_LEN, j->result, sizeof(j->result));
	} else if (!strcmp(algorithm, "bcrypt")) {
		struct crypt_data *data = calloc(1, sizeof(*data));
		char setting[40];
		if (!data) { j->error = -1; return NULL; }
		snprintf(setting, sizeof(setting), "$2b$%02u$abcdefghijklmnopqrstuu", work);
		char *result = crypt_r(password, setting, data);
		if (!result || strlen(result) != 60 || strncmp(result, "$2b$", 4)) j->error = -1;
		else memcpy(j->result, result+28, 32);
		free(data);
	} else if (!strcmp(algorithm, "pbkdf2")) {
		j->error = !PKCS5_PBKDF2_HMAC(password, strlen(password), salt, SALT_LEN,
			work, EVP_sha256(), sizeof(j->result), j->result);
	} else j->error = -1;
	j->wall = seconds()-start;
	return NULL;
}

static int selftest(void)
{
	unsigned char out[32]; char hex[65];
	if (argon2id_hash_raw(2, 256, 1, "password", 8, "somesalt", 8, out, 32)) return 1;
	for (int i=0;i<32;i++) snprintf(hex+i*2, 3, "%02x", out[i]);
	if (strcmp(hex, "9dfeb910e80bad0311fee20f9c0e2b12c17987b4cac90c2ef54d5b3021c68bfe")) return 1;
	if (!PKCS5_PBKDF2_HMAC("password",8,(const unsigned char *)"salt",4,1,EVP_sha256(),32,out)) return 1;
	for (int i=0;i<32;i++) snprintf(hex+i*2, 3, "%02x", out[i]);
	if (strcmp(hex, "120fb6cffcf8b32c43e7225256c4f837a86548c92ccc35480805987cb70be17b")) return 1;
	struct crypt_data *data = calloc(1, sizeof(*data));
	if (!data) return 1;
	char *hash = crypt_r("password", "$2b$04$abcdefghijklmnopqrstuu", data);
	if (!hash || strcmp(hash,"$2b$04$abcdefghijklmnopqrstuughE8Ev8uGFaUgY2cNEySvxngrb/Jzdm")) { free(data); return 1; }
	printf("selftest argon2id=PASS pbkdf2_sha256=PASS bcrypt=%s\n", hash);
	free(data);
	return 0;
}

static unsigned number(const char *s, unsigned min, unsigned max)
{
	char *end; errno=0; unsigned long n = strtoul(s, &end, 10);
	if (errno || !*s || *end || n < min || n > max) { fprintf(stderr,"invalid argument\n"); exit(2); }
	return n;
}

int main(int argc, char **argv)
{
	if (argc == 2 && !strcmp(argv[1], "selftest")) return selftest();
	if (argc == 2 && !strcmp(argv[1], "inventory")) {
		int fd = socket(AF_ALG, SOCK_SEQPACKET, 0), error = errno;
		printf("openssl=%s argon2_version=%d cpus=%ld af_alg_socket=%s errno=%d\n",
			OpenSSL_version(OPENSSL_VERSION),ARGON2_VERSION_NUMBER,sysconf(_SC_NPROCESSORS_ONLN),
			fd >= 0 ? "available" : "unavailable", fd >= 0 ? 0 : error);
		if (fd >= 0) close(fd);
		return 0;
	}
	if (argc != 7) { fprintf(stderr,"usage: password-bench algorithm work memory_kib lanes jobs repeats\n"); return 2; }
	algorithm=argv[1]; work=number(argv[2],1,1200000); memory_kib=number(argv[3],0,65536);
	lanes=number(argv[4],1,4); jobs=number(argv[5],1,2); unsigned repeats=number(argv[6],1,5);
	if ((!strcmp(algorithm,"bcrypt") && (work<4 || work>12)) ||
	    (!strcmp(algorithm,"argon2id") && (work>5 || memory_kib<256)) ||
	    (strcmp(algorithm,"argon2id") && strcmp(algorithm,"bcrypt") && strcmp(algorithm,"pbkdf2"))) return 2;
	struct rlimit limit = {160UL*1024*1024,160UL*1024*1024};
	if (setrlimit(RLIMIT_AS,&limit)) { perror("setrlimit"); return 2; }
	setvbuf(stdout,NULL,_IOLBF,0);
	/* Provider initialization is not part of the repeated steady-state hashes. */
	unsigned char warm[32];
	if (!PKCS5_PBKDF2_HMAC("test",4,salt,SALT_LEN,1,EVP_sha256(),32,warm)) return 2;
	for (unsigned iteration=0;iteration<repeats;iteration++) {
		long available=read_memory("/proc/meminfo","MemAvailable:");
		long need = !strcmp(algorithm,"argon2id") ? (long)memory_kib * jobs : 0;
		if (available < need + 32*1024) { fprintf(stderr,"SKIP: insufficient reserve for requested profile\n"); return 77; }
		struct monitor m = {.done=0,.min_available=available,.peak_rss=0,.max_temp=-1};
		pthread_t monitor_thread, workers[2]; struct job result[2] = {0};
		struct cpu_sample before[MAX_CPUS+1],after[MAX_CPUS+1]; struct rusage a,b;
		cpus(before); getrusage(RUSAGE_SELF,&a);
		double start=seconds();
		if (pthread_create(&monitor_thread,NULL,monitor,&m)) return 2;
		if (jobs==1) hash_job(&result[0]);
		else {
			for (unsigned j=0;j<jobs;j++) if (pthread_create(&workers[j],NULL,hash_job,&result[j])) _exit(2);
			for (unsigned j=0;j<jobs;j++) pthread_join(workers[j],NULL);
		}
		double wall=seconds()-start;
		getrusage(RUSAGE_SELF,&b); cpus(after);
		atomic_store(&m.done,1); pthread_join(monitor_thread,NULL);
		for (unsigned j=0;j<jobs;j++) if (result[j].error) { fprintf(stderr,"hash error=%d\n",result[j].error); return 1; }
		double user=(b.ru_utime.tv_sec-a.ru_utime.tv_sec)+(b.ru_utime.tv_usec-a.ru_utime.tv_usec)/1e6;
		double system=(b.ru_stime.tv_sec-a.ru_stime.tv_sec)+(b.ru_stime.tv_usec-a.ru_stime.tv_usec)/1e6;
		printf("{\"algorithm\":\"%s\",\"work\":%u,\"memory_kib\":%u,\"lanes\":%u,\"jobs\":%u,\"trial\":%u,"
			"\"wall_s\":%.6f,\"login0_s\":%.6f,\"hashes_per_s\":%.6f,\"user_s\":%.6f,\"system_s\":%.6f,"
			"\"process_cpu_pct_one_vpe\":%.2f,\"machine_busy_pct\":%.2f,\"peak_rss_kib\":%ld,\"min_available_kib\":%ld,"
			"\"max_temp_mC\":%ld,\"minor_faults\":%ld,\"major_faults\":%ld,\"voluntary_switches\":%ld,\"involuntary_switches\":%ld,\"vpe_busy_pct\":[",
			algorithm,work,memory_kib,lanes,jobs,iteration,wall,result[0].wall,jobs/wall,user,system,(user+system)/wall*100,
			utilization(before[0],after[0]),b.ru_maxrss,m.min_available,m.max_temp,b.ru_minflt-a.ru_minflt,
			b.ru_majflt-a.ru_majflt,b.ru_nvcsw-a.ru_nvcsw,b.ru_nivcsw-a.ru_nivcsw);
		for(int i=1;i<=MAX_CPUS;i++) printf("%s%.2f",i==1?"":",",utilization(before[i],after[i]));
		puts("]}");
	}
	return 0;
}
