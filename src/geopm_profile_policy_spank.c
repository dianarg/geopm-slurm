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
#include "geopm_endpoint.h"
#include "geopm_agent.h"
#include "config.h"

SPANK_PLUGIN(geopm_profile_policy, 1);

int slurm_spank_init(spank_t spank_ctx, int argc, char **argv);



int get_user_profile_policy(uid_t user_id, const char *agent, const char* profile, int num_policy, double *policy_vals)
{
    int err = 0;
    struct passwd *pwd = getpwuid(user_id);
    const char *username = pwd->pw_name;
    const char *user_home = pwd->pw_dir;
    char policy_db[NAME_MAX];
    strncpy(policy_db, user_home, NAME_MAX);
    strncat(policy_db, "/policystore.db", NAME_MAX - strnlen(user_home, NAME_MAX));
    err = geopm_policystore_connect(policy_db);
    if (err) {
        slurm_info("geopm_policystore_connect(%s) failed", policy_db);
        return ESPANK_ERROR;
    }

    err = geopm_policystore_get_best(profile, agent, num_policy, policy_vals);
    if (err) {
        slurm_info("geopm_policystore_get_best(%s, %s, %d, _) failed with error %d", profile, agent, num_policy, err);
        return ESPANK_ERROR;
    }

    err = geopm_policystore_disconnect();
    if (err) {
        slurm_info("geopm_policystore_disconnect() failed");
        return ESPANK_ERROR;
    }
    return 0;
}

int get_agent_profile_attached(struct geopm_endpoint_c *endpoint, size_t agent_size, char *agent,
                               size_t profile_size, char *profile)
{
    int err = 0;
    // get agent and profile name of attached controller
    /// @todo: need a timeout
    while(!err && strnlen(agent, GEOPM_ENDPOINT_AGENT_NAME_MAX) == 0) {
        err = geopm_endpoint_agent(endpoint, GEOPM_ENDPOINT_AGENT_NAME_MAX, agent);
    }
    if (err) {
        slurm_info("geopm_endpoint_agent() failed.");
    }
    if (!err) {
        err = geopm_endpoint_profile_name(endpoint, GEOPM_ENDPOINT_PROFILE_NAME_MAX, profile);
    }
    if (err) {
        slurm_info("geopm_endpoint_profile_name() failed.");
    }
}

int slurm_spank_init(spank_t spank_ctx, int argc, char **argv)
{
    /* only activate in remote context */
    if (spank_remote(spank_ctx) != 1) {
        return ESPANK_SUCCESS;
    }

    ///@todo: get path to policy store from argv.  this is set in args in plugstack.conf
    /// can also configure the endpoint name there.  that string needs to match what's
    /// set in environment-override


    uint32_t job_id;
    uint32_t step_id;
    uid_t user_id;
    spank_get_item(spank_ctx, S_JOB_ID, &job_id);
    spank_get_item(spank_ctx, S_JOB_STEPID, &step_id);
    spank_get_item(spank_ctx, S_JOB_UID, &user_id);

    slurm_info("Loaded geopm_profile_policy plugin: spank_init");
    slurm_info("spank_init: Job: %d, Job step: %d, user: %d", job_id, step_id, user_id);

    /// @todo: use geopm_error_message(int err, char *msg, size_t size)
    int err = 0;
    // create endpoint and wait for attach
    struct geopm_endpoint_c *endpoint;
    char agent[GEOPM_ENDPOINT_AGENT_NAME_MAX];
    char profile[GEOPM_ENDPOINT_PROFILE_NAME_MAX];
    int num_policy = 0;
    double *policy_vals = NULL;

    memset(agent, 0, GEOPM_ENDPOINT_AGENT_NAME_MAX);

    err = geopm_endpoint_create("/geopm_endpoint_test", &endpoint);
    if (err) {
        slurm_info("geopm_endpoint_create() failed");
    }

    if (!err) {
        err = geopm_endpoint_open(endpoint);
    }
    if (err) {
        slurm_info("geopm_endpoint_open() failed.");
    }

    err = get_agent_profile_attached(endpoint,
                                     GEOPM_ENDPOINT_AGENT_NAME_MAX, agent,
                                     GEOPM_ENDPOINT_PROFILE_NAME_MAX, profile);

    // allocate array for policy
    if (!err) {
        err = geopm_agent_num_policy(agent, num_policy);
    }
    if (err) {
        slurm_info("geopm_agent_num_policy(%s, _) failed", agent);
    }

    if (!err) {
        policy_vals = (double*)malloc(num_policy * sizeof(double));
    }
    if (!policy_vals) {
        slurm_info("malloc() failed");
        err = ESPANK_ERROR;
    }

    // look up policy
    if (!err) {
        err = get_user_profile_policy(user_id, agent, profile, num_policy, policy_vals);
    }

    // write policy
    if (!err) {
        err = geopm_endpoint_write_policy(endpoint, num_policy, policy_vals);
        if (err) {
            slurm_info("geopm_endpoint_write_policy() failed.");
        }
    }
    free(policy_vals);

    err = geopm_endpoint_close(endpoint);
    if (err) {
        slurm_info("geopm_endpoint_close() failed");
    }
    err = geopm_endpoint_destroy(endpoint);
    if (err) {
        slurm_info("geopm_endpoint_destroy() failed");
    }

    return err;
}
