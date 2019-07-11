/*
 * Copyright (c) 2019, Intel Corporation
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in
 *       the documentation and/or other materials provided with the
 *       distribution.
 *
 *     * Neither the name of Intel Corporation nor the names of its
 *       contributors may be used to endorse or promote products derived
 *       from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY LOG OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <limits.h>
#include <math.h>
#include <stdint.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <pwd.h>

#include <slurm/spank.h>

#include "geopm_agent.h"
#include "geopm_pio.h"
#include "geopm_error.h"
#include "geopm_policystore.h"
#include "geopm_daemon.h"
#include "geopm_agent.h"
#include "config.h"

SPANK_PLUGIN(geopm_profile_policy, 1);

int slurm_spank_init(spank_t spank_ctx, int argc, char **argv);
int slurm_spank_init_post_opt(spank_t spank_ctx, int argc, char **argv);

static size_t g_profile_size;
static char g_profile[NAME_MAX];
static size_t g_agent_size;
static char g_agent[NAME_MAX];

/* callback to save profile string */
static int set_policy_for_profile(int val, const char *optarg, int remote)
{
    if (optarg) {
        slurm_info("profile name is %s, remote: %d", optarg, remote);
        g_profile_size = strlen(optarg);
        strncpy(g_profile, optarg, NAME_MAX - 1);
    }
    else {
        slurm_info("callback triggered but optarg was null");
    }
    return 0;
}

/* callback to save agent */
static int set_agent_for_profile(int val, const char *optarg, int remote)
{
    if (optarg) {
        slurm_info("agent name is %s, remote: %d", optarg, remote);
        g_agent_size = strlen(optarg);
        strncpy(g_agent, optarg, NAME_MAX - 1);
    }
    else {
        slurm_info("callback triggered but optarg was null");
    }
    return 0;
}

int slurm_spank_init(spank_t spank_ctx, int argc, char **argv)
{
    struct spank_option profile_option =
    {
        "geopm-profile",  /* name */
        "GEOPM profile name",  /* arginfo */
        "If matching profile is found, sets custom GEOPM policy for the job", /* usage */
        1, /* has_arg; 1 = requires argument */
        0, /* val */
        (spank_opt_cb_f)set_policy_for_profile  /* callback */
    };
    struct spank_option agent_option =
    {
        "geopm-agent",  /* name */
        "GEOPM agent name",  /* arginfo */
        "If matching profile is found, sets custom GEOPM policy for the job", /* usage */
        1, /* has_arg; 1 = requires argument */
        0, /* val */
        (spank_opt_cb_f)set_agent_for_profile  /* callback */
    };

    uint32_t job_id;
    uint32_t step_id;
    uid_t user_id;
    spank_get_item(spank_ctx, S_JOB_ID, &job_id);
    spank_get_item(spank_ctx, S_JOB_STEPID, &step_id);
    spank_get_item(spank_ctx, S_JOB_UID, &user_id);

    slurm_info("Loaded geopm_profile_policy plugin: spank_init");
    slurm_info("spank_init: Job: %d, Job step: %d, user: %d", job_id, step_id, user_id);
    g_profile_size = 0;
    memset(g_profile, 0, NAME_MAX);
    g_agent_size = 0;
    memset(g_agent, 0, NAME_MAX);
    int err = spank_option_register(spank_ctx, &profile_option);
    if (err != ESPANK_SUCCESS) {
        slurm_info("Failed to register profile option.");
        return err;
    }
    err = spank_option_register(spank_ctx, &agent_option);
    if (err != ESPANK_SUCCESS) {
        slurm_info("Failed to register agent option.");
        return err;
    }
    return ESPANK_SUCCESS;
}

int slurm_spank_init_post_opt(spank_t spank_ctx, int argc, char **argv)
{
    /* only activate in remote context */
    if (spank_remote(spank_ctx) != 1) {
        return ESPANK_SUCCESS;
    }

    uint32_t job_id;
    uint32_t step_id;
    uid_t user_id;
    spank_get_item(spank_ctx, S_JOB_ID, &job_id);
    spank_get_item(spank_ctx, S_JOB_STEPID, &step_id);
    spank_get_item(spank_ctx, S_JOB_UID, &user_id);
    struct passwd *pwd = getpwuid(user_id);
    const char *username = pwd->pw_name;
    slurm_info("Loaded geopm_profile_policy plugin: spank_init_post_opt");
    slurm_info("spank_init_post_opt: Job: %d, Job step: %d, user: %d, username: %s", job_id, step_id, user_id, username);

    int err = 0;

    if (g_profile_size == 0) {
        /* no profile provided; geopm_manager_get_best_policy will provide a default */
        geopm_daemon_get_env_agent(NAME_MAX, g_agent);
    }
    else if (g_profile_size > 0 && g_agent_size == 0) {
        slurm_info("Error: --geopm-agent option must be provided if --geopm-profile is used.");
        return ESPANK_BAD_ARG;
    }
    else {
        /* agent must match environment override */
        char agent[NAME_MAX];
        geopm_daemon_get_env_agent(NAME_MAX, agent);
        if (strncmp(g_agent, agent, NAME_MAX) != 0) {
            slurm_info("Error: --geopm-agent option must match override.");
            return ESPANK_BAD_ARG;
        }
    }

    int num_policy = 0;
    err = geopm_agent_num_policy(g_agent, &num_policy);
    if (err) {
        slurm_info("geopm_agent_num_policy(%s, _) failed", g_agent);
        return ESPANK_ERROR;
    }

    const char *user_home = pwd->pw_dir;
    char policy_db[NAME_MAX];
    strncpy(policy_db, user_home, NAME_MAX);
    strncat(policy_db, "/policystore.db", NAME_MAX - strnlen(user_home, NAME_MAX));
    err = geopm_policystore_connect(policy_db);
    if (err) {
        slurm_info("geopm_policystore_connect(%s) failed", policy_db);
        return ESPANK_ERROR;
    }

    double *policy_vals = (double*)malloc(num_policy * sizeof(double));
    if (!policy_vals) {
        slurm_info("malloc() failed", g_agent);
        return ESPANK_ERROR;
    }
    err = geopm_policystore_get_best(g_profile, g_agent, num_policy, policy_vals);
    if (err) {
        slurm_info("geopm_policystore_get_best(%s, %s, %d, _) failed with error %d", g_profile, g_agent, num_policy, err);
        return ESPANK_ERROR;
    }

    err = geopm_policystore_disconnect();
    if (err) {
        slurm_info("geopm_policystore_disconnect() failed");
        return ESPANK_ERROR;
    }

    char policy_json[NAME_MAX];
    err = geopm_agent_policy_json(g_agent, policy_vals, NAME_MAX, policy_json);
    if (err) {
        slurm_info("geopm_agent_policy_json(%s, %s, %d, _) failed", g_agent, policy_vals, NAME_MAX);
        return ESPANK_ERROR;
    }

    /* Note: this path should match the GEOPM_POLICY setting in
     * /etc/geopm/environment-*.json.
     */
    const char *policy_path = "/etc/geopm/node_policy.json";
    char hostname[NAME_MAX];
    gethostname(hostname, NAME_MAX);
    err = geopm_daemon_set_host_policy(hostname, policy_path, policy_json);
    if (err) {
        slurm_info("geopm_daemon_set_policy(%s, %s, %s) failed", hostname, policy_path, policy_json);
        return ESPANK_ERROR;
    }

    return 0;
}
