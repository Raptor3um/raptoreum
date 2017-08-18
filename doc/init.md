Sample init scripts and service configuration for raptoreumd
==========================================================

Sample scripts and configuration files for systemd, Upstart and OpenRC
can be found in the contrib/init folder.

    contrib/init/raptoreumd.service:    systemd service unit configuration
    contrib/init/raptoreumd.openrc:     OpenRC compatible SysV style init script
    contrib/init/raptoreumd.openrcconf: OpenRC conf.d file
    contrib/init/raptoreumd.conf:       Upstart service configuration file
    contrib/init/raptoreumd.init:       CentOS compatible SysV style init script

Service User
---------------------------------

All three Linux startup configurations assume the existence of a "raptoreumcore" user
and group.  They must be created before attempting to use these scripts.
The OS X configuration assumes raptoreumd will be set up for the current user.

Configuration
---------------------------------

At a bare minimum, raptoreumd requires that the rpcpassword setting be set
when running as a daemon.  If the configuration file does not exist or this
setting is not set, raptoreumd will shutdown promptly after startup.

This password does not have to be remembered or typed as it is mostly used
as a fixed token that raptoreumd and client programs read from the configuration
file, however it is recommended that a strong and secure password be used
as this password is security critical to securing the wallet should the
wallet be enabled.

If raptoreumd is run with the "-server" flag (set by default), and no rpcpassword is set,
it will use a special cookie file for authentication. The cookie is generated with random
content when the daemon starts, and deleted when it exits. Read access to this file
controls who can access it through RPC.

By default the cookie is stored in the data directory, but it's location can be overridden
with the option '-rpccookiefile'.

This allows for running raptoreumd without having to do any manual configuration.

`conf`, `pid`, and `wallet` accept relative paths which are interpreted as
relative to the data directory. `wallet` *only* supports relative paths.

For an example configuration file that describes the configuration settings,
see `contrib/debian/examples/raptoreum.conf`.

Paths
---------------------------------

### Linux

All three configurations assume several paths that might need to be adjusted.

Binary:              `/usr/bin/raptoreumd`  
Configuration file:  `/etc/raptoreumcore/raptoreum.conf`  
Data directory:      `/var/lib/raptoreumd`  
PID file:            `/var/run/raptoreumd/raptoreumd.pid` (OpenRC and Upstart) or `/var/lib/raptoreumd/raptoreumd.pid` (systemd)  
Lock file:           `/var/lock/subsys/raptoreumd` (CentOS)  

The configuration file, PID directory (if applicable) and data directory
should all be owned by the raptoreumcore user and group.  It is advised for security
reasons to make the configuration file and data directory only readable by the
raptoreumcore user and group.  Access to raptoreum-cli and other raptoreumd rpc clients
can then be controlled by group membership.

### Mac OS X

Binary:              `/usr/local/bin/raptoreumd`  
Configuration file:  `~/Library/Application Support/RaptoreumCore/raptoreum.conf`  
Data directory:      `~/Library/Application Support/RaptoreumCore`
Lock file:           `~/Library/Application Support/RaptoreumCore/.lock`

Installing Service Configuration
-----------------------------------

### systemd

Installing this .service file consists of just copying it to
/usr/lib/systemd/system directory, followed by the command
`systemctl daemon-reload` in order to update running systemd configuration.

To test, run `systemctl start raptoreumd` and to enable for system startup run
`systemctl enable raptoreumd`

### OpenRC

Rename raptoreumd.openrc to raptoreumd and drop it in /etc/init.d.  Double
check ownership and permissions and make it executable.  Test it with
`/etc/init.d/raptoreumd start` and configure it to run on startup with
`rc-update add raptoreumd`

### Upstart (for Debian/Ubuntu based distributions)

Drop raptoreumd.conf in /etc/init.  Test by running `service raptoreumd start`
it will automatically start on reboot.

NOTE: This script is incompatible with CentOS 5 and Amazon Linux 2014 as they
use old versions of Upstart and do not supply the start-stop-daemon utility.

### CentOS

Copy raptoreumd.init to /etc/init.d/raptoreumd. Test by running `service raptoreumd start`.

Using this script, you can adjust the path and flags to the raptoreumd program by
setting the RAPTOREUMD and FLAGS environment variables in the file
/etc/sysconfig/raptoreumd. You can also use the DAEMONOPTS environment variable here.

### Mac OS X

Copy org.raptoreum.raptoreumd.plist into ~/Library/LaunchAgents. Load the launch agent by
running `launchctl load ~/Library/LaunchAgents/org.raptoreum.raptoreumd.plist`.

This Launch Agent will cause raptoreumd to start whenever the user logs in.

NOTE: This approach is intended for those wanting to run raptoreumd as the current user.
You will need to modify org.raptoreum.raptoreumd.plist if you intend to use it as a
Launch Daemon with a dedicated raptoreumcore user.

Auto-respawn
-----------------------------------

Auto respawning is currently only configured for Upstart and systemd.
Reasonable defaults have been chosen but YMMV.
