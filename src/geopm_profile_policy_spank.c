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
#include <pthread.h>

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
int slurm_spank_exit(spank_t spank_ctx, int argc, char **argv);

int get_profile_policy(const char *db_path, const char *agent, const char* profile,
                       int num_policy, double *policy_vals);
int get_agent_profile_attached(struct geopm_endpoint_c *endpoint, size_t agent_size, char *agent,
                               size_t profile_size, char *profile);

// function to run in thread
void *wait_endpoint_attach_policy(void *args);

static volatile int g_continue;
static volatile int g_run_app;
static int g_err;
static pthread_t g_thread_id;
static char g_db_path[NAME_MAX];


int slurm_spank_init(spank_t spank_ctx, int argc, char **argv)
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

    slurm_info("Loaded geopm_profile_policy plugin");
    slurm_info("Job: %d, Job step: %d, user: %d", job_id, step_id, user_id);

    ///@todo: get path to policy store from argv.  this is set in args
    /// in plugstack.conf can also configure the endpoint name there.
    /// that string needs to match what's set in environment-override
    if (argc > 1) {
        strncpy(g_db_path, argv[1], NAME_MAX);
    }
    else {
        strncpy(g_db_path, "/home/drguttma/policystore.db", NAME_MAX);
        slurm_info("No db_path argument provided, using default: %s", g_db_path);
    }

    g_continue = 1;
    g_run_app = 0;
    int err = pthread_create(&g_thread_id, NULL, wait_endpoint_attach_policy, g_db_path);
    if (err) {
        slurm_info("pthread_create() failed.");
    }
    while (!err && g_run_app == 0) {
        // wait for endpoint to create shared memory and lock it
    }
    return err;
}

int slurm_spank_exit(spank_t spank_ctx, int argc, char **argv)
{
    /* only activate in remote context */
    if (spank_remote(spank_ctx) != 1) {
        return ESPANK_SUCCESS;
    }
    g_continue = 0;
    void *thread_err = NULL;
    ssize_t err = pthread_join(g_thread_id, &thread_err);
    if (err) {
        slurm_info("pthread_join() failed");
    }
    else {
        err = (ssize_t)thread_err;
    }
    return err;
}

int get_profile_policy(const char *db_path, const char *agent, const char* profile, int num_policy, double *policy_vals)
{
    int err = 0;
    err = geopm_policystore_connect(db_path);
    if (err) {
        slurm_info("geopm_policystore_connect(%s) failed", db_path);
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
    int err = geopm_endpoint_agent(endpoint, GEOPM_ENDPOINT_AGENT_NAME_MAX, agent);
    if (err) {
        slurm_info("geopm_endpoint_agent() failed: %d", err);
        return err;
    }
    err = geopm_endpoint_profile_name(endpoint, GEOPM_ENDPOINT_PROFILE_NAME_MAX, profile);
    if (err) {
        slurm_info("geopm_endpoint_profile_name() failed.");
        return err;
    }
    return err;
}

void *wait_endpoint_attach_policy(void *args)
{
    /// @todo: use geopm_error_message(int err, char *msg, size_t size)
    ssize_t err = 0;
    // create endpoint and wait for attach
    struct geopm_endpoint_c *endpoint;
    char agent[GEOPM_ENDPOINT_AGENT_NAME_MAX];
    char profile[GEOPM_ENDPOINT_PROFILE_NAME_MAX];
    int num_policy = 0;
    double *policy_vals = NULL;
    char *db_path = (char *)args;

    slurm_info("using %s", db_path);

    memset(agent, 0, GEOPM_ENDPOINT_AGENT_NAME_MAX);

    const char *endpoint_shmem = "geopm_endpoint_test";

    slurm_info("creating endpoint at %s", endpoint_shmem);
    err = geopm_endpoint_create(endpoint_shmem, &endpoint);
    if (err) {
        slurm_info("geopm_endpoint_create() failed");
        goto exit1;
    }

    err = geopm_endpoint_open(endpoint);
    if (err) {
        slurm_info("geopm_endpoint_open() failed.");
        goto exit2;
    }

    g_run_app = 1;

    // Timeout is required for srun launches that don't start a GEOPM controller.
    slurm_info("wait for GEOPM controller attach");
    double time_remaining = 100.0;
    while (!err && g_continue && time_remaining > 0.0 &&
           strnlen(agent, GEOPM_ENDPOINT_AGENT_NAME_MAX) == 0) {
        err = get_agent_profile_attached(endpoint,
                                         GEOPM_ENDPOINT_AGENT_NAME_MAX, agent,
                                         GEOPM_ENDPOINT_PROFILE_NAME_MAX, profile);
        sleep(1);
        time_remaining -= 1.0;
    }
    if (time_remaining <= 0.0) {
        slurm_info("Timed out waiting for agent to attach. Endpoint shutting down.");
        goto exit3;
    }
    if (err || !g_continue) {
        slurm_info("cancel wait for Controller");
        goto exit3;
    }

    slurm_info("get policy");
    // allocate array for policy
    err = geopm_agent_num_policy(agent, &num_policy);
    if (err) {
        slurm_info("geopm_agent_num_policy(%s, _) failed", agent);
        goto exit3;
    }
    policy_vals = (double*)malloc(num_policy * sizeof(double));
    if (!policy_vals) {
        slurm_info("malloc() failed");
        err = ESPANK_ERROR;
        goto exit3;
    }

    // look up policy
    err = get_profile_policy(db_path, agent, profile, num_policy, policy_vals);
    if (err) {
        goto exit4;
    }

    slurm_info("write_policy");
    // write policy
    err = geopm_endpoint_write_policy(endpoint, num_policy, policy_vals);
    if (err) {
        slurm_info("geopm_endpoint_write_policy() failed.");
        goto exit4;
    }
exit4:
    free(policy_vals);
exit3:
    geopm_endpoint_close(endpoint);
    slurm_info("endpoint closed");
exit2:
    geopm_endpoint_destroy(endpoint);
exit1:

    return (void*)err;
}
