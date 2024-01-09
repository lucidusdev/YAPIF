# Remove Play Services from Magisk Denylist when set to enforcing
if magisk --denylist status; then
    magisk --denylist rm com.google.android.gms
fi

# Conditional early sensitive properties
MODDIR=${0%/*}
SYSPROP=$MODDIR/system_properties
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

if ! grep -Fxq "RESETPROP=1" $MODDIR/yapif.ini; then
	# RootBeer, Microsoft
	resetprop_if_diff ro.build.tags release-keys

	# Samsung
	resetprop_if_diff ro.boot.warranty_bit 0
	resetprop_if_diff ro.vendor.boot.warranty_bit 0
	resetprop_if_diff ro.vendor.warranty_bit 0
	resetprop_if_diff ro.warranty_bit 0

	# OnePlus
	resetprop_if_diff ro.is_ever_orange 0

	# Other
	resetprop_if_diff ro.build.type user
	resetprop_if_diff ro.debuggable 0
	resetprop_if_diff ro.secure 1
fi