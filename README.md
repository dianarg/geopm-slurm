GEOPM SPANK PLUGINS FOR SLURM
=============================

This repository maintains the spank plugins to support the GEOPM
runtime with the SLURM resource manager.

SETUP
-----
0. Install the geopmpolicy RPM on head and compute nodes.

1. Create a /etc/geopm directory on the compute nodes.

2. Create /etc/geopm/environment-default.json and/or
/etc/geopm/environment-override.json on the compute nodes with the
following contents:

```
    {
        "GEOPM_POLICY": "/etc/geopm/node_policy.json",
	"GEOPM_AGENT": "power_balancer"
    }
```

Note that the value of GEOPM_AGENT may be configured according to the
requirements of the system, but the path to the policy in GEOPM_POLICY
must match the path used inside the geopm_profile_policy plugin for
that plugin to work (see geopm_profile_policy_spank.c).  Later this
may be a configure-time option for this plugin.

3. Create or append the following to /etc/slurm/plugstack.conf on the
compute nodes and the head node, replacing /geopm-slurm/install/path
with the install location of the plugins in this repo:

```
optional /geopm-slurm/install/path/lib/geopm_spank/libgeopm_spank.so
optional /geopm-slurm/install/path/lib/geopm_spank/libgeopm_profile_policy_spank.so
```

3a. Alternatively, configure Slurm to find plugins in a different
location with PluginDir; then the full path is not needed.  While
the plugin is under development, a path into a user install may
be more convenient.


USING THE STATIC POLICY PLUGIN
------------------------------
The static policy will be set for all jobs when the geopm_spank plugin
is loaded.  The policy and agent used will be determined by the
environment in /etc/geopm.  Jobs that use GEOPM will pick up the
environment settings from /etc/geopm/environment*.  If only
/etc/geopm/environment-default.json is present, the user's commandline
may override the default environment.  If
/etc/geopm/environment-override.json is present, all GEOPM runs will use
the override environment values.


USING THE PROFILE-BASED POLICY PLUGIN
-------------------------------------
When the geopm_profile_policy_spank plugin is loaded, and the user
provides a --geopm-profile argument to `srun`, GEOPM will attempt to
use the profile name to determine the optimal policy.  Note that if
/etc/geopm/environment-override.json was *not* created during setup
and the user also provides a --geopm-policy argument, the optimal
policy determined by GEOPM will be overridden by the user-provided
policy.

TESTING THE PROFILE PLUGIN
--------------------------
1. Reserve the target compute nodes and setup as described above. E.g.:

    sudo scontrol create reservation ReservationName=diana Nodes=mr-fusion2,mr-fusion3 starttime=now Users=drguttma duration=0

2. Log in to each compute node as root and `tail -f /var/log/slurm.log` to
   watch the slurm log.

3. To test with salloc + srun, pass the reservation name to salloc:

    salloc -N2 --reservation=diana
    srun ...

4. To test with srun only, pass the reservation name to srun:

    srun -N1 --reservation=diana  ...

5. To trigger the plugin logic, pass a --geopm-profile argument to
   srun.  The appropriate policy should appear in the report.
   Currently, the plugin uses a default policy unless the profile is
   of the form "power_NUMBER", in which case NUMBER is used to set
   the power cap.  You can target a specific node with `srun -w <node>`

TODO/KNOWN ISSUES
-----------------

- The launcher does not correctly handle all combinations of -w and
  --reservation.

- Multinode runs on mr-fusion seem to be broken.

- Need to figure out how to determine agent on the head node.