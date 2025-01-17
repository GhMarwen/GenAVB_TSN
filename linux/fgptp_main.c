/*
* Copyright 2015 Freescale Semiconductor, Inc.
* Copyright 2019-2021 NXP
* 
* NXP Confidential. This software is owned or controlled by NXP and may only 
* be used strictly in accordance with the applicable license terms.  By expressly 
* accepting such terms or by downloading, installing, activating and/or otherwise 
* using the software, you are agreeing that you have read, and that you agree to 
* comply with and are bound by, such license terms.  If you do not agree to be 
* bound by the applicable license terms, then you may not retain, install, activate 
* or otherwise use the software.
*/

/**
 @file
 @brief Top fgptp Linux process
 @details Setups linux fgptp process and thread for NXP GPTP stack component.
 */

#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <getopt.h>
#include <time.h>
#include <fcntl.h>

#include "common/log.h"
#include "common/version.h"
#include "common/clock.h"
#include "common/ptp.h"

#include "genavb/helpers.h"

#include "linux/cfgfile.h"
#include "linux/log.h"

#include "gptp/config.h"

#include "fgptp.h"
#include "init.h"


#define FGPTP_VERSION GENAVB_VERSION

/*
 * Default configuration file(s), if none are specified on cmd line:
 * as well as <CONF_FILE_NAME>-N for other domains
 */
#define FGPTP_CONF_FILENAME "/etc/genavb/fgptp.cfg"
#define CONF_FILE_LEN	256

#ifdef CONFIG_MANAGEMENT
void *management_thread_main(void *arg);
#endif

#ifdef CONFIG_GPTP
void *gptp_thread_main(void *arg);
#endif

static int terminate = 0;

static void sigterm_hdlr(int signum)
{
	os_log(LOG_INIT, "signum(%d)\n", signum);

	terminate = 1;
}

void print_version(void)
{
	printf("fgptp version %s\n", FGPTP_VERSION);
	printf("Developped by NXP\n");
}

void print_usage (void)
{
	printf("\nUsage:\n fgptp [options]\n");
	printf("\nOptions:\n"
		"\t-v                    display program version\n"
		"\t-b                    start in bridge mode\n"
		"\t-f <config file>      fgptp configuration filename (for domain0), with domainN configuration file being <config file>-N\n"
		"\t-h                    print this help text\n");
}

#ifdef CONFIG_GPTP
static int log_string2level(const char *s)
{
	int level;

	for (level = LOG_CRIT; level <= LOG_DEBUG; level++) {
		if (!strcasecmp(s, log_lvl_string[level]))
			return level;
	}

	return -1;
}

static int process_section_general(struct _SECTIONENTRY *configtree, int instance_index, struct fgptp_config *cfg)
{
	int rc = 0;
	u64 gm_id;
	int level;
	char stringvalue[CFG_STRING_MAX_LEN] = "";

	/* gPTP domain */
	if (cfg_get_signed_int(configtree, "FGPTP_GENERAL", "domain_number",
				    !instance_index ? 0 : -1, -1, PTP_DOMAIN_NUMBER_MAX,
				    &cfg->domain_cfg[instance_index].domain_number) < 0) {
		rc = -1;
		goto exit;
	}

	/* Below parameters are only needed for domain 0 */
	if (instance_index != 0)
		goto exit;

	/* profile */
	if (cfg_get_string(configtree, "FGPTP_GENERAL", "profile", CFG_GPTP_DEFAULT_PROFILE_NAME, stringvalue)) {
		rc = -1;
		goto exit;
	}

	if (!strcmp(stringvalue, "automotive"))
		cfg->profile = CFG_GPTP_PROFILE_AUTOMOTIVE;
	else
		cfg->profile = CFG_GPTP_PROFILE_STANDARD;

	/* grandmaster ID (or ClockIdentity)*/
	if (cfg->profile == CFG_GPTP_PROFILE_AUTOMOTIVE) {
		/* Per 802.1AS - 8.5.2.2.1 -
		When using an EUI-48, the first 3 octets, i.e., the OUI portion, of the IEEE EUI-48 are assigned in order to
		the first 3 octets of the clockIdentity with most significant octet of the IEEE EUI-64, i.e., the most significant octet of the
		OUI portion, assigned to the clockIdentity octet array member with index 0. Octets with index 3 and 4 have hex values
		0xFF and 0xFE respectively. The remaining 3 octets of the IEEE EUI-48 are assigned in order to the last 3 octets of the
		clockIdentity */
		if (cfg_get_u64(configtree, "FGPTP_GENERAL", "gm_id", CFG_GPTP_DEFAULT_GM_ID, 0x000000FFFE000000, 0xFFFFFFFFFEFFFFFF, &gm_id)) {
			rc = -1;
			goto exit;
		}

		/* overwrite default grandmaster id if specified */
		cfg->gm_id = htonll(gm_id);
	} else {
		cfg->gm_id = 0; /*will be determined by BMCA */
	}

	/* log level */
	if (cfg_get_string(configtree, "FGPTP_GENERAL", "log_level", CFG_GPTP_DEFAULT_LOG_LEVEL, stringvalue)) {
		rc = -1;
		goto exit;
	}

	level = log_string2level(stringvalue);
	if (level < 0) {
		printf("Error setting log level (%s)\n", stringvalue);
		rc = -1;
		goto exit;
	}

	cfg->log_level = level;

	/* log_monotonic */
	if (cfg_get_string(configtree, "FGPTP_GENERAL", "log_monotonic", CFG_GPTP_DEFAULT_LOG_MONOTONIC, stringvalue)) {
		rc = -1;
		goto exit;
	}

	if (!strcmp(stringvalue, "enabled"))
		log_enable_monotonic();

	/* neighbor propagation delay threshold */
	if (cfg_get_u64(configtree, "FGPTP_GENERAL", "neighborPropDelayThreshold", CFG_GPTP_NEIGH_THRESH_DEFAULT, CFG_GPTP_NEIGH_THRESH_MIN_DEFAULT, CFG_GPTP_NEIGH_THRESH_MAX_DEFAULT, &cfg->neighborPropDelayThreshold)) {
		rc = -1;
		goto exit;
	}

	/* reverse sync feature */
	if (cfg_get_uint(configtree, "FGPTP_GENERAL", "reverse_sync", CFG_GPTP_RSYNC_ENABLE_DEFAULT, CFG_GPTP_RSYNC_ENABLE_MIN_DEFAULT, CFG_GPTP_RSYNC_ENABLE_MAX_DEFAULT, &cfg->rsync)) {
		rc = -1;
		goto exit;
	}

	if (cfg_get_uint(configtree, "FGPTP_GENERAL", "reverse_sync_interval", CFG_GPTP_RSYNC_INTERVAL_DEFAULT, CFG_GPTP_RSYNC_INTERVAL_MIN_DEFAULT, CFG_GPTP_RSYNC_INTERVAL_MAX_DEFAULT, &cfg->rsync_interval)) {
		rc = -1;
		goto exit;
	}

	if (cfg_get_uint(configtree, "FGPTP_GENERAL", "statsInterval", CFG_GPTP_STATS_INTERVAL_DEFAULT, CFG_GPTP_STATS_INTERVAL_MIN_DEFAULT, CFG_GPTP_STATS_INTERVAL_MAX_DEFAULT, &cfg->statsInterval)) {
		rc = -1;
		goto exit;
	}

	/* IEEE 802.1AS-2011 interoperability mode */
	if (cfg_get_string(configtree, "FGPTP_GENERAL", "force_2011", CFG_GPTP_DEFAULT_FORCE_2011_STRING, stringvalue)) {
		rc = -1;
		goto exit;
	}

	if (!strcmp(stringvalue, "yes"))
		cfg->force_2011 = 1;
	else
		cfg->force_2011 = 0;

exit:
	return rc;
}

static int process_section_gm_params(struct _SECTIONENTRY *configtree, struct fgptp_domain_config *cfg)
{
	int rc = 0;

	/* gm capable */
	if (cfg_get_uchar(configtree, "FGPTP_GM_PARAMS", "gmCapable", CFG_GPTP_DEFAULT_GM_CAPABLE, 0, 1, &cfg->gmCapable)) {
		rc = -1;
		goto exit;
	}

	/* priority 1 */
	if (cfg_get_uchar(configtree, "FGPTP_GM_PARAMS", "priority1", CFG_GPTP_DEFAULT_PRIORITY1, 0, 255, &cfg->priority1)) {
		rc = -1;
		goto exit;
	}

	/* priority 2 */
	if (cfg_get_uchar(configtree, "FGPTP_GM_PARAMS", "priority2", CFG_GPTP_DEFAULT_PRIORITY2, 0, 255, &cfg->priority2)) {
		rc = -1;
		goto exit;
	}

	/* clock class */
	if (cfg_get_uchar(configtree, "FGPTP_GM_PARAMS", "clockClass", CFG_GPTP_DEFAULT_CLOCK_CLASS, 0, 255, &cfg->clockClass)) {
		rc = -1;
		goto exit;
	}

	/* clock accuracy */
	if (cfg_get_uchar(configtree, "FGPTP_GM_PARAMS", "clockAccuracy", CFG_GPTP_DEFAULT_CLOCK_ACCURACY, 0, 0xFF, &cfg->clockAccuracy)) {
		rc = -1;
		goto exit;
	}

	/* clock variance */
	if (cfg_get_ushort(configtree, "FGPTP_GM_PARAMS", "offsetScaledLogVariance", CFG_GPTP_DEFAULT_CLOCK_VARIANCE, 0, 0xFFFF, &cfg->offsetScaledLogVariance)) {
		rc = -1;
		goto exit;
	}

exit:
	return rc;
}

static int process_section_automotive_params(struct _SECTIONENTRY *configtree, struct gptp_linux_config *linux_cfg)
{
	struct fgptp_config *cfg = &linux_cfg->gptp_cfg;
	char stringvalue[CFG_STRING_MAX_LEN] = "";
	u32 initial_neighborPropDelay = 0, neighborPropDelay_sensitivity = 0;
	int rc = 0;
	int i;

	/* automotive pdelay mode */
	if (cfg_get_string(configtree, "FGPTP_AUTOMOTIVE_PARAMS", "neighborPropDelay_mode", CFG_GPTP_DEFAULT_PDELAY_MODE_STRING, stringvalue)) {
		rc = -1;
		goto exit;
	}

	if (!strcmp(stringvalue, "silent"))
		cfg->neighborPropDelay_mode = CFG_GPTP_PDELAY_MODE_SILENT;
	else if (!strcmp(stringvalue, "static"))
		cfg->neighborPropDelay_mode = CFG_GPTP_PDELAY_MODE_STATIC;
	else
		cfg->neighborPropDelay_mode = CFG_GPTP_PDELAY_MODE_STANDARD;

	/* initial pdelay value in ns unit */
	if (cfg_get_u32(configtree, "FGPTP_AUTOMOTIVE_PARAMS", "initial_neighborPropDelay", CFG_GPTP_DEFAULT_PDELAY_VALUE, CFG_GPTP_DEFAULT_PDELAY_VALUE_MIN, CFG_GPTP_DEFAULT_PDELAY_VALUE_MAX, &initial_neighborPropDelay)) {
		rc = -1;
		goto exit;
	}
	/* applying default value to all port */
	for (i = 0; i < CFG_GPTP_MAX_NUM_PORT; i++)
		cfg->initial_neighborPropDelay[i] = initial_neighborPropDelay;

	/* pdelay sensitivity in ns unit */
	if (cfg_get_u32(configtree, "FGPTP_AUTOMOTIVE_PARAMS", "neighborPropDelay_sensitivity", CFG_GPTP_DEFAULT_PDELAY_SENSITIVITY, CFG_GPTP_DEFAULT_PDELAY_SENSITIVITY_MIN, CFG_GPTP_DEFAULT_PDELAY_SENSITIVITY_MAX, &neighborPropDelay_sensitivity)) {
		rc = -1;
		goto exit;
	}
	cfg->neighborPropDelay_sensitivity = (ptp_double)neighborPropDelay_sensitivity;

	/* automotive nvram file location */
	if (cfg_get_string(configtree, "FGPTP_AUTOMOTIVE_PARAMS", "nvram_file", "/etc/genavb/fgptp.nvram", linux_cfg->nvram_file)) {
		rc = -1;
		goto exit;
	}

exit:
	return rc;
}



static int process_section_port_params(struct _SECTIONENTRY *configtree, int instance_index, struct fgptp_config *cfg)
{
	char stringvalue[CFG_STRING_MAX_LEN] = "";
	int rc = 0;
	int i;
	char section[64];
	unsigned char has_slave_port = 0;

	/* Per port settings */
	for (i = 0; i < CFG_MAX_NUM_PORT; i++) {
		if(snprintf(section, 64, "FGPTP_PORT%d", i + 1) < 0) { /* first port has index 1 in configuration file */
			rc = -1;
			goto exit;
		}

		/* Peer delay mechanism */
		if (cfg_get_string(configtree, section, "delayMechanism", (instance_index == 0)? "P2P" : "COMMON_P2P", stringvalue)) {
			rc = -1;
			goto exit;
		}
		if(!strcasecmp("P2P", stringvalue))
			cfg->port_cfg[i].delayMechanism[instance_index] = P2P;
		else if (!strcasecmp("COMMON_P2P", stringvalue))
			cfg->port_cfg[i].delayMechanism[instance_index] = COMMON_P2P;
		else {
			rc = -1;
			goto exit;
		}

		/* Below parameters are only needed for domain 0 */
		if (instance_index != 0)
			continue;

		/* Port's Role */
		if (cfg_get_string(configtree, section, "portRole", "disabled", stringvalue)) {
			rc = -1;
			goto exit;
		}

		if(!strcasecmp("disabled", stringvalue))
			cfg->port_cfg[i].portRole = DISABLED_PORT;
		else if (!strcasecmp("master", stringvalue))
			cfg->port_cfg[i].portRole = MASTER_PORT;
		else if (!strcasecmp("slave", stringvalue)) {
			if (!has_slave_port) {
				cfg->port_cfg[i].portRole = SLAVE_PORT;
				has_slave_port = 1;
			} else {
				/* only one slave port is possible per time aware bridge */
				rc = -1;
				goto exit;
			}
		} else
			cfg->port_cfg[i].portRole = CFG_GPTP_DEFAULT_PORT_ROLE;

		/* Port's ptpPortEnabled */
		if (cfg_get_uchar(configtree, section, "ptpPortEnabled", CFG_GPTP_DEFAULT_PTP_ENABLED, CFG_GPTP_DEFAULT_PTP_ENABLED_MIN, CFG_GPTP_DEFAULT_PTP_ENABLED_MAX, &cfg->port_cfg[i].ptpPortEnabled)) {
			rc = -1;
			goto exit;
		}

		/*Port's Rx/Tx delays compensation */
		if (cfg_get_signed_int(configtree, section, "rxDelayCompensation", CFG_GPTP_DEFAULT_RX_DELAY_COMP, CFG_GPTP_DEFAULT_DELAY_COMP_MIN, CFG_GPTP_DEFAULT_DELAY_COMP_MAX, &cfg->port_cfg[i].rxDelayCompensation)) {
			rc = -1;
			goto exit;
		}

		if (cfg_get_signed_int(configtree, section, "txDelayCompensation", CFG_GPTP_DEFAULT_TX_DELAY_COMP, CFG_GPTP_DEFAULT_DELAY_COMP_MIN, CFG_GPTP_DEFAULT_DELAY_COMP_MAX, &cfg->port_cfg[i].txDelayCompensation)) {
			rc = -1;
			goto exit;
		}

		/* initial pdelay request transmit interval */
		if (cfg_get_schar(configtree, section, "initialLogPdelayReqInterval", CFG_GPTP_DFLT_LOG_PDELAY_REQ_INTERVAL, CFG_GPTP_MIN_LOG_PDELAY_REQ_INTERVAL, CFG_GPTP_MAX_LOG_PDELAY_REQ_INTERVAL, &cfg->port_cfg[i].initialLogPdelayReqInterval)) {
			rc = -1;
			goto exit;
		}

		/*  initial sync transmit interval  */
		if (cfg_get_schar(configtree, section, "initialLogSyncInterval", CFG_GPTP_DFLT_LOG_SYNC_INTERVAL, CFG_GPTP_MIN_LOG_SYNC_INTERVAL, CFG_GPTP_MAX_LOG_SYNC_INTERVAL, &cfg->port_cfg[i].initialLogSyncInterval)) {
			rc = -1;
			goto exit;
		}

		/*  initial announce transmit interval	*/
		if (cfg_get_schar(configtree, section, "initialLogAnnounceInterval", CFG_GPTP_DFLT_LOG_ANNOUNCE_INTERVAL, CFG_GPTP_MIN_LOG_ANNOUNCE_INTERVAL, CFG_GPTP_MAX_LOG_ANNOUNCE_INTERVAL, &cfg->port_cfg[i].initialLogAnnounceInterval)) {
			rc = -1;
			goto exit;
		}


		/* initial pdelay request transmit interval */
		if (cfg_get_schar(configtree, section, "operLogPdelayReqInterval", CFG_GPTP_DFLT_LOG_PDELAY_REQ_INTERVAL, CFG_GPTP_MIN_LOG_PDELAY_REQ_INTERVAL, CFG_GPTP_MAX_LOG_PDELAY_REQ_INTERVAL, &cfg->port_cfg[i].operLogPdelayReqInterval)) {
			rc = -1;
			goto exit;
		}

		/*  initial sync transmit interval  */
		if (cfg_get_schar(configtree, section, "operLogSyncInterval", CFG_GPTP_DFLT_LOG_SYNC_INTERVAL, CFG_GPTP_MIN_LOG_SYNC_INTERVAL, CFG_GPTP_MAX_LOG_SYNC_INTERVAL, &cfg->port_cfg[i].operLogSyncInterval)) {
			rc = -1;
			goto exit;
		}
	}
exit:
	return rc;
}

static int process_config(struct gptp_linux_config *linux_cfg, struct _SECTIONENTRY *configtree[])
{
	struct fgptp_config *cfg = &linux_cfg->gptp_cfg;
	int i;

	/********************************/
	/* fetch values from configtree */
	/********************************/

	for (i = 0; i < CFG_MAX_GPTP_DOMAINS; i++) {
		if (process_section_general(configtree[i], i, cfg))
			goto exit;

		if (process_section_gm_params(configtree[i], &cfg->domain_cfg[i]))
			goto exit;

		if (process_section_port_params(configtree[i], i, cfg))
			goto exit;
	}

	if (process_section_automotive_params(configtree[0], linux_cfg))
		goto exit;

	return 0;

exit:
	printf("FGPTP cfg file: Error while parsing config file\n");

	return -1;
}

static int fgptp_clock_time_init(os_clock_id_t clk_id)
{
	struct timespec system_time;
	struct tm localtime;
	u64 system_time_ns;
	char validtime[26];

	if (clock_gettime(CLOCK_REALTIME, &system_time)) {
		os_log(LOG_CRIT, "clock_gettime(): %s\n", strerror(errno));
		goto err;
	}

	system_time_ns = system_time.tv_sec * 1000000000ULL + system_time.tv_nsec;
	if (clock_set_time64(clk_id, system_time_ns)) {
		os_log(LOG_CRIT, "clock_set_time64() for fgptp clock target %d failed \n", clk_id);
		goto err;
	}

	if (localtime_r(&system_time.tv_sec, &localtime) && asctime_r(&localtime, validtime))
		os_log(LOG_INFO, "Setting gPTP time for clock target %d to system clock: %s \n", clk_id, validtime);
	else
		os_log(LOG_ERR, "Setting gPTP time for clock target %d to system clock: *Invalid time format* \n", clk_id);

	return 0;
err:
	return -1;
}

#endif


/*******************************************************************************
* @function_name main
* @brief Linux main entry point
*
*/
int main(int argc, char *argv[])
{
	struct fgptp_ctx fgptp;
	struct fgptp_config *gptp_cfg = &fgptp.gptp_linux_cfg.gptp_cfg;
	struct os_net_config net_config = { .net_mode = CONFIG_FGPTP_DEFAULT_NET };
	sigset_t set;
	struct sigaction action;
#ifdef CONFIG_MANAGEMENT
	pthread_t management_thread;
#endif
#ifdef CONFIG_GPTP
	pthread_t gptp_thread;
#endif
	int i, option;
	int rc = -1;
	const char *fgptp_conf_filename;
	char file_name[CONF_FILE_LEN + 4];
	struct _SECTIONENTRY *configtree[CFG_MAX_GPTP_DOMAINS];
	int fd;
	unsigned int bridge_logical_port_list[CFG_BR_DEFAULT_NUM_PORTS] = CFG_BR_LOGICAL_PORT_LIST;
	unsigned int endpoint_logical_port_list[CFG_EP_DEFAULT_NUM_PORTS] = CFG_EP_LOGICAL_PORT_LIST;
	unsigned int *logical_port_list;
	bool is_bridge = false;


	/* Setup standard output in append mode so that log file truncate works correctly */
	fd = fileno(stdout);
	if (fd < 0)
		goto err_fileno;

	if (fcntl(fd, F_SETFL, O_APPEND) < 0)
		goto err_fcntl;

	printf("\nNXP's gPTP application version %s (Built %s %s)\n\n", FGPTP_VERSION, __DATE__, __TIME__);

	/*
	* Application arguments parsing
	*/

	fgptp_conf_filename = FGPTP_CONF_FILENAME;

	while ((option = getopt(argc, argv,"vhbf:")) != -1) {
		switch (option) {
		case 'v':
			print_version();
			goto exit;
			break;

		case 'b':
			is_bridge = true;
			break;

		case 'f':
			fgptp_conf_filename = optarg;
			break;

		case 'h':
		default:
			print_usage();
			goto exit;
		}
	}

	memset(&fgptp, 0, sizeof(struct fgptp_ctx));

#ifdef CONFIG_GPTP
	/*
	* Get configuration parameters
	*/
	printf("FGPTP: Using configuration file(s): %s (and %s-N domain variants, if provided)\n", fgptp_conf_filename, fgptp_conf_filename);

	/* read all sections and all key/value pairs from config file(s), and store them in chained list */
	configtree[0] = cfg_read(fgptp_conf_filename);
	if (configtree[0] == NULL) {
		printf("Error: failed to read config file %s\n", fgptp_conf_filename);
		goto err_config;
	}

	for (i = 1; i < CFG_MAX_GPTP_DOMAINS; i++) {
		snprintf(file_name, CONF_FILE_LEN + 4, "%s-%1d", fgptp_conf_filename, i);
		configtree[i] = cfg_read(file_name);
		if (configtree[i] == NULL)
			printf("Warning: failed to read config file %s\n", file_name);
	}

	rc = process_config(&fgptp.gptp_linux_cfg, configtree);

	/* finished parsing the configuration tree, so free memory */
	for (i = 0; i < CFG_MAX_GPTP_DOMAINS; i++)
		cfg_free_configtree(configtree[i]);

	if (rc) /* Cfg file processing failed, exit */
		goto err_config;

	log_level_set(os_COMPONENT_ID, gptp_cfg->log_level);
#endif

	/*
	* Osal initialization
	*/
	if (os_init(&net_config) < 0)
		goto err_osal;

#if defined(CONFIG_MANAGEMENT) || defined(CONFIG_GPTP)
	if (is_bridge)
		logical_port_list = bridge_logical_port_list;
	else
		logical_port_list = endpoint_logical_port_list;
#endif

#ifdef CONFIG_MANAGEMENT
	fgptp.management_cfg.log_level = LOG_INFO;

	fgptp.management_cfg.is_bridge = is_bridge;

	if (is_bridge)
		fgptp.management_cfg.port_max = CFG_BR_DEFAULT_NUM_PORTS;
	else
		fgptp.management_cfg.port_max = CFG_EP_DEFAULT_NUM_PORTS;

	for (i = 0; i < fgptp.management_cfg.port_max; i++)
		fgptp.management_cfg.logical_port_list[i] = logical_port_list[i];
#endif

#ifdef CONFIG_GPTP

	gptp_cfg->domain_max = CFG_MAX_GPTP_DOMAINS;

	gptp_cfg->is_bridge = is_bridge;

	if (is_bridge) {
		gptp_cfg->port_max = CFG_BR_DEFAULT_NUM_PORTS;
		fgptp.gptp_linux_cfg.clock_log = OS_CLOCK_GPTP_BR_0_0;
	} else {
		gptp_cfg->port_max = CFG_EP_DEFAULT_NUM_PORTS;
		fgptp.gptp_linux_cfg.clock_log = OS_CLOCK_GPTP_EP_0_0;
	}

	for (i = 0; i < gptp_cfg->port_max; i++)
		gptp_cfg->logical_port_list[i] = logical_port_list[i];

#ifdef CONFIG_MANAGEMENT
	gptp_cfg->management_enabled = 1;
#else
	gptp_cfg->management_enabled = 0;
#endif

	/*
	* Clocks
	*/
	gptp_cfg->clock_local = logical_port_to_local_clock(logical_port_list[CFG_DEFAULT_PORT_ID]);

	for (i = 0; i < gptp_cfg->domain_max; i++) {
		gptp_cfg->domain_cfg[i].clock_target = logical_port_to_gptp_clock(logical_port_list[CFG_DEFAULT_PORT_ID], i);
		gptp_cfg->domain_cfg[i].clock_source = gptp_cfg->domain_cfg[i].clock_target;
	}
#endif

	/* Block most signals for all threads */
	if (sigfillset(&set) < 0)
		os_log(LOG_ERR, "sigfillset(): %s\n", strerror(errno));

	if (sigdelset(&set, SIGILL) < 0)
		os_log(LOG_ERR, "sigdelset(): %s\n", strerror(errno));

	if (sigdelset(&set, SIGFPE) < 0)
		os_log(LOG_ERR, "sigdelset(): %s\n", strerror(errno));

	if (sigdelset(&set, SIGSEGV) < 0)
		os_log(LOG_ERR, "sigdelset(): %s\n", strerror(errno));

	rc = pthread_sigmask(SIG_BLOCK, &set, NULL);
	if (rc)
		os_log(LOG_ERR, "pthread_sigmask(): %s\n", strerror(rc));

#ifdef CONFIG_MANAGEMENT
	pthread_cond_init(&fgptp.management_cond, NULL);
#endif
#ifdef CONFIG_GPTP
	pthread_cond_init(&fgptp.gptp_cond, NULL);
#endif

#ifdef CONFIG_MANAGEMENT
	rc = pthread_create(&management_thread, NULL, management_thread_main, &fgptp);
	if (rc) {
		os_log(LOG_CRIT, "pthread_create(): %s\n", strerror(rc));
		goto err_pthread_create_management;
	}

	pthread_mutex_lock(&fgptp.status_mutex);

	while (!fgptp.management_status) pthread_cond_wait(&fgptp.management_cond, &fgptp.status_mutex);

	pthread_mutex_unlock(&fgptp.status_mutex);

	if (fgptp.management_status < 0)
		goto err_thread_init_management;
#endif

#ifdef CONFIG_GPTP

	/* Set the gPTP time to the system time to have a reasonable time
	 * in case of being selected as gPTP GM*/

	for (i = 0; i < CFG_DEFAULT_GPTP_DOMAINS; i++) {
		if (fgptp_clock_time_init(gptp_cfg->domain_cfg[i].clock_target) < 0 ) {
			os_log(LOG_CRIT, "fgptp_clock_time_init failed \n");
			goto err_clock_time_init;
		}
	}

	rc = pthread_create(&gptp_thread, NULL, gptp_thread_main, &fgptp);
	if (rc) {
		os_log(LOG_CRIT, "pthread_create(): %s\n", strerror(rc));
		goto err_pthread_create_gptp;
	}

	pthread_mutex_lock(&fgptp.status_mutex);

	while (!fgptp.gptp_status) pthread_cond_wait(&fgptp.gptp_cond, &fgptp.status_mutex);

	pthread_mutex_unlock(&fgptp.status_mutex);

	if (fgptp.gptp_status < 0)
		goto err_thread_init_gptp;
#endif

	action.sa_handler = sigterm_hdlr;
	action.sa_flags = 0;

	if (sigemptyset(&action.sa_mask) < 0)
		os_log(LOG_ERR, "sigemptyset(): %s\n", strerror(errno));

	if (sigaction(SIGTERM, &action, NULL) < 0)
		os_log(LOG_ERR, "sigaction(): %s\n", strerror(errno));

	if (sigaction(SIGQUIT, &action, NULL) < 0)
		os_log(LOG_ERR, "sigaction(): %s\n", strerror(errno));

	if (sigaction(SIGINT, &action, NULL) < 0)
		os_log(LOG_ERR, "sigaction(): %s\n", strerror(errno));

	/* Unblock normal process control signals for the main thread only */
	if (sigemptyset(&set) < 0)
		os_log(LOG_ERR, "sigfillset(): %s\n", strerror(errno));

	if (sigaddset(&set, SIGINT) < 0)
		os_log(LOG_ERR, "sigaddset(): %s\n", strerror(errno));

	if (sigaddset(&set, SIGTERM) < 0)
		os_log(LOG_ERR, "sigaddset(): %s\n", strerror(errno));

	if (sigaddset(&set, SIGQUIT) < 0)
		os_log(LOG_ERR, "sigaddset(): %s\n", strerror(errno));

	if (sigaddset(&set, SIGTSTP) < 0)
		os_log(LOG_ERR, "sigaddset(): %s\n", strerror(errno));

	rc = pthread_sigmask(SIG_UNBLOCK, &set, NULL);
	if (rc)
		os_log(LOG_ERR, "sigprocmask(): %s\n", strerror(rc));

	while (1) {
		sleep(1);

		if (terminate)
			break;
	}

#ifdef CONFIG_GPTP
	pthread_cancel(gptp_thread);
	pthread_join(gptp_thread, NULL);
#endif

#ifdef CONFIG_MANAGEMENT
	pthread_cancel(management_thread);
	pthread_join(management_thread, NULL);
#endif

	os_exit();

	return 0;

#ifdef CONFIG_GPTP
err_thread_init_gptp:
	pthread_cancel(gptp_thread);
	pthread_join(gptp_thread, NULL);

err_pthread_create_gptp:
err_clock_time_init:
#endif
#ifdef CONFIG_MANAGEMENT
err_thread_init_management:
	pthread_cancel(management_thread);
	pthread_join(management_thread, NULL);

err_pthread_create_management:
#endif
	os_exit();
err_osal:
err_config:
exit:
err_fcntl:
err_fileno:
	return -1;
}

