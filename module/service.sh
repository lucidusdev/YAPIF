# Conditional sensitive properties


MODDIR=${0%/*}
SYSPROP=$MODDIR/sysprop
if ! grep -Fxq "SYSPROP=1" $MODDIR/yapif.ini; then 
	SYSPROP=resetprop
fi


resetprop_if_diff() {
    local NAME=$1
    local EXPECTED=$2
    local CURRENT=$(resetprop $NAME)

    [ -z "$CURRENT" ] || [ "$CURRENT" == "$EXPECTED" ] || $SYSPROP $NAME $EXPECTED
}

resetprop_if_match() {
    local NAME=$1
    local CONTAINS=$2
    local VALUE=$3

    [[ "$(resetprop $NAME)" == *"$CONTAINS"* ]] && $SYSPROP $NAME $VALUE
}

# Magisk recovery mode
resetprop_if_match ro.bootmode recovery unknown
resetprop_if_match ro.boot.mode recovery unknown
resetprop_if_match vendor.boot.mode recovery unknown

# SELinux
if [ -n "$(resetprop ro.build.selinux)" ]; then
    resetprop --delete ro.build.selinux
fi

# use toybox to protect *stat* access time reading
if [ "$(toybox cat /sys/fs/selinux/enforce)" == "0" ]; then
    chmod 640 /sys/fs/selinux/enforce
    chmod 440 /sys/fs/selinux/policy
fi

# SafetyNet/Play Integrity
# late props which must be set after boot_completed for various OEMs
until [ "$(getprop sys.boot_completed)" == "1" ]; do
	sleep 1
done

#both Java&JNI need this, must set globally.

$SYSPROP ro.boot.flash.locked 1

# use SPOOFPROP=1 to have these set INSIDE YAPIF, otherwise, set it here...
if ! grep -Fxq "SPOOFPROP=1" $MODDIR/yapif.ini; then
    resetprop_if_diff ro.boot.vbmeta.device_state locked
    resetprop_if_diff vendor.boot.verifiedbootstate green
    resetprop_if_diff ro.boot.verifiedbootstate green
    resetprop_if_diff ro.boot.veritymode enforcing
    resetprop_if_diff vendor.boot.vbmeta.device_state locked
fi

