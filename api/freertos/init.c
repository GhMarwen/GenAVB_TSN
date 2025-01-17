/*
* Copyright 2018, 2020 NXP
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
 \file init.c
 \brief GenAVB public API initialization for freertos
 \details API definition for the GenAVB library
 \copyright Copyright 2018, 2020 NXP
*/

#include "api/init.h"

#include "common/log.h"
#include "common/version.h"

#include "freertos/stats_task.h"
#include "freertos/net_task.h"
#include "freertos/ipc.h"
#include "freertos/media_queue.h"
#include "freertos/media_clock.h"
#include "freertos/hw_timer.h"
#include "freertos/hr_timer.h"
#include "freertos/gpt.h"
#include "freertos/fqtss.h"
#include "freertos/clock.h"

#ifdef CONFIG_MANAGEMENT
extern TaskHandle_t management_task_init(struct management_config *cfg);
extern void management_task_exit(TaskHandle_t task);
extern struct management_config management_default_config;
static struct management_config *management_current_config = &management_default_config;
#endif
#ifdef CONFIG_GPTP
extern TaskHandle_t gptp_task_init(struct fgptp_config *cfg);
extern void gptp_task_exit(TaskHandle_t task);
extern struct fgptp_config gptp_default_config;
static struct fgptp_config *gptp_current_config = &gptp_default_config;
#endif
#ifdef CONFIG_SRP
extern TaskHandle_t srp_task_init(struct srp_config *cfg);
extern void srp_task_exit(TaskHandle_t task);
extern struct srp_config srp_default_config;
static struct srp_config *srp_current_config = &srp_default_config;
#endif
#ifdef CONFIG_AVTP
void streaming_exit(struct genavb_handle *genavb);
extern TaskHandle_t avtp_task_init(struct avtp_config *cfg);
extern void avtp_task_exit(TaskHandle_t task);
extern struct avtp_config avtp_default_config;
static struct avtp_config *avtp_current_config = &avtp_default_config;
#endif
#ifdef CONFIG_AVDECC
extern TaskHandle_t avdecc_task_init(struct avdecc_config *cfg);
extern void avdecc_task_exit(TaskHandle_t task);
extern struct avdecc_config avdecc_default_config;
static struct avdecc_config *avdecc_current_config = &avdecc_default_config;
#endif

__init void genavb_get_default_config(struct genavb_config *config)
{
#ifdef CONFIG_MANAGEMENT
	memcpy(&config->management_config, &management_default_config, sizeof(struct management_config));
#endif
#ifdef CONFIG_GPTP
	memcpy(&config->fgptp_config, &gptp_default_config, sizeof(struct fgptp_config));
#endif
#ifdef CONFIG_SRP
	memcpy(&config->srp_config, &srp_default_config, sizeof(struct srp_config));
#endif
#ifdef CONFIG_AVTP
	memcpy(&config->avtp_config, &avtp_default_config, sizeof(struct avtp_config));
#endif
#ifdef CONFIG_AVDECC
	memcpy(&config->avdecc_config, &avdecc_default_config, sizeof(struct avdecc_config));
#endif
}

__init void genavb_set_config(struct genavb_config *config)
{
#ifdef CONFIG_MANAGEMENT
	management_current_config = &config->management_config;
#endif
#ifdef CONFIG_GPTP
	gptp_current_config = &config->fgptp_config;
#endif
#ifdef CONFIG_SRP
	srp_current_config = &config->srp_config;
#endif
#ifdef CONFIG_AVTP
	avtp_current_config = &config->avtp_config;
#endif
#ifdef CONFIG_AVDECC
	avdecc_current_config = &config->avdecc_config;
#endif
}

__init static int osal_init(void)
{
	if (stats_task_init() < 0)
		goto err_stats;

	if (mclock_init() < 0)
		goto err_mclock;

	if (media_queue_init() < 0)
		goto err_media_queue;

	if (hw_avb_timer_init() < 0)
		goto err_hw_avb_timer;

	if (hw_timer_init() < 0)
		goto err_hw_timer;

	if (gpt_driver_init() < 0)
		goto err_gpt;

	if (port_init() < 0)
		goto err_port;

	if (os_clock_init() < 0)
		goto err_clock;

	if (hr_timer_init() < 0)
		goto err_timer;

	if (net_task_init() < 0)
		goto err_net;

	if (port_post_init() < 0)
		goto err_port_post;

	if (ipc_init() < 0)
		goto err_ipc;

	if (fqtss_init() < 0)
		goto err_fqtss;

	hw_avb_timer_start();

	return 0;

err_fqtss:
	ipc_exit();

err_ipc:
	port_pre_exit();

err_port_post:
	net_task_exit();

err_net:
	hr_timer_exit();

err_timer:
	os_clock_exit();

err_clock:
	port_exit();

err_port:
	gpt_driver_exit();

err_gpt:
	hw_timer_exit();

err_hw_timer:
	hw_avb_timer_exit();

err_hw_avb_timer:
	media_queue_exit();

err_media_queue:
	mclock_exit();

err_mclock:
	stats_task_exit();

err_stats:

	return -1;
}

__exit static void osal_exit(void)
{
	fqtss_exit();
	ipc_exit();
	port_pre_exit();
	net_task_exit();
	hr_timer_exit();
	os_clock_exit();
	port_exit();
	gpt_driver_exit();
	hw_timer_exit();
	hw_avb_timer_exit();
	media_queue_exit();
	mclock_exit();
	stats_task_exit();
}

__init int genavb_init(struct genavb_handle **genavb, unsigned int flags)
{
	int rc = -GENAVB_ERR_NO_MEMORY;

	log_level_set(api_COMPONENT_ID, LOG_INIT);
	log_level_set(common_COMPONENT_ID, LOG_INIT);
	log_level_set(os_COMPONENT_ID, LOG_INFO);

	os_log(LOG_INIT, "NXP's GenAVB/TSN stack version %s (Built %s %s)\n", GENAVB_VERSION, __DATE__, __TIME__);

	*genavb = pvPortMalloc(sizeof(struct genavb_handle));
	if (!(*genavb)) {
		rc = -GENAVB_ERR_NO_MEMORY;
		goto err_handle;
	}

	memset(*genavb, 0, sizeof(struct genavb_handle));

	(*genavb)->flags = flags;
	(*genavb)->flags &= ~AVTP_INITIALIZED;

	if (osal_init() < 0)
		goto err_osal;

#ifdef CONFIG_MANAGEMENT
	(*genavb)->management_handle = management_task_init(management_current_config);
	if (!(*genavb)->management_handle)
		goto err_management;
#endif
#ifdef CONFIG_GPTP
	(*genavb)->gptp_handle = gptp_task_init(gptp_current_config);
	if (!(*genavb)->gptp_handle)
		goto err_gptp;
#endif
#ifdef CONFIG_SRP
	(*genavb)->srp_handle = srp_task_init(srp_current_config);
	if (!(*genavb)->srp_handle)
		goto err_srp;
#endif
#ifdef CONFIG_AVTP
	(*genavb)->avtp_handle = avtp_task_init(avtp_current_config);
	if (!(*genavb)->avtp_handle)
		goto err_avtp;
#endif
#ifdef CONFIG_AVDECC
	(*genavb)->avdecc_handle = avdecc_task_init(avdecc_current_config);
	if (!(*genavb)->avdecc_handle)
		goto err_avdecc;
#endif
	return GENAVB_SUCCESS;

#ifdef CONFIG_AVDECC
err_avdecc:
#endif
#ifdef CONFIG_AVTP
	avtp_task_exit((*genavb)->avtp_handle);
err_avtp:
#endif
#ifdef CONFIG_SRP
	srp_task_exit((*genavb)->srp_handle);
err_srp:
#endif
#ifdef CONFIG_GPTP
	gptp_task_exit((*genavb)->gptp_handle);
err_gptp:
#endif
#ifdef CONFIG_MANAGEMENT
	management_task_exit((*genavb)->management_handle);

err_management:
#endif
	osal_exit();

err_osal:
	vPortFree(*genavb);

err_handle:
	*genavb = NULL;

	return rc;
}


__exit int genavb_exit(struct genavb_handle *genavb)
{
#ifdef CONFIG_AVTP
	streaming_exit(genavb);
#endif
#ifdef CONFIG_AVDECC
	avdecc_task_exit(genavb->avdecc_handle);
#endif
#ifdef CONFIG_AVTP
	avtp_task_exit(genavb->avtp_handle);
#endif
#ifdef CONFIG_SRP
	srp_task_exit(genavb->srp_handle);
#endif
#ifdef CONFIG_GPTP
	gptp_task_exit(genavb->gptp_handle);
#endif
#ifdef CONFIG_MANAGEMENT
	management_task_exit(genavb->management_handle);
#endif

	osal_exit();
	vPortFree(genavb);

	return GENAVB_SUCCESS;
}
