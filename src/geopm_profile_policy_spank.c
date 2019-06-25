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

#include <slurm/spank.h>

#include "geopm_agent.h"
#include "geopm_pio.h"
#include "geopm_error.h"
#include "geopm_manager.h"
#include "config.h"

SPANK_PLUGIN(geopm_profile_policy, 1);

int slurm_spank_init(spank_t spank_ctx, int argc, char **argv);
int slurm_spank_init_post_opt(spank_t spank_ctx, int argc, char **argv);

static size_t g_profile_size;
static char g_profile[NAME_MAX];
static int g_remote;

/* callback to save profile string */
static int set_policy_for_profile(int val, const char *optarg, int remote)
{
    g_remote = remote;
    if (optarg) {
        slurm_info("profile name is %s, remote: %d", optarg, remote);
        g_profile_size = strlen(optarg);
        strncpy(g_profile, optarg, NAME_MAX-1);
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
    g_remote = 0;
    int err = spank_option_register(spank_ctx, &profile_option);
    if (err != ESPANK_SUCCESS) {
        slurm_info("Failed to register option.");
        return err;
    }
    return 0;
}

int slurm_spank_init_post_opt(spank_t spank_ctx, int argc, char **argv)
{
    uint32_t job_id;
    uint32_t step_id;
    uid_t user_id;
    spank_get_item(spank_ctx, S_JOB_ID, &job_id);
    spank_get_item(spank_ctx, S_JOB_STEPID, &step_id);
    spank_get_item(spank_ctx, S_JOB_UID, &user_id);

    slurm_info("Loaded geopm_profile_policy plugin: spank_init_post_opt");
    slurm_info("spank_init_post_opt: Job: %d, Job step: %d, user: %d", job_id, step_id, user_id);

    /* todo: check error return codes */
    char hostname[NAME_MAX];
    gethostname(hostname, NAME_MAX);

    if (g_profile_size == 0) {
        /* no profile provided; geopm_manager_get_best_policy will provide a default */
        g_profile[0] = '\0';
    }
    if (!g_remote) {
        /* head node decides policy */
        /* todo: move get_best_policy() here and put policy in environment */
    }
    if (g_remote) {
        /* todo: get policy from environment */
        char agent[NAME_MAX];
        char policy_json[NAME_MAX];
        /* Note: this path should match the GEOPM_POLICY setting in
         * /etc/geopm/environment-*.json.
         */
        const char *policy_path = "/etc/geopm/node_policy.json";
        geopm_env_agent(NAME_MAX, agent);
        geopm_manager_get_best_policy(g_profile, agent, NAME_MAX, policy_json);
        geopm_manager_set_host_policy(hostname, policy_path, policy_json);
    }
    return 0;
}
