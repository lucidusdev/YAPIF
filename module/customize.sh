# Error on < Android 8.
if [ "$API" -lt 26 ]; then
    abort "- !!! You can't use this module on Android < 8.0"
fi


# safetynet-fix module is obsolete and it's incompatible with PIF.
if [ -d /data/adb/modules/safetynet-fix ]; then
	rm -rf /data/adb/modules/safetynet-fix
	rm -f /data/adb/SNFix.dex
    ui_print "! safetynet-fix module will be removed. Do NOT install it again along PIF."
fi


# Disable MagiskHidePropsConf and PlayIntergrityFix
M="MagiskHidePropsConf PlayIntegrityFix"
for i in $M; do
	if [ -d /data/adb/modules/$i ]; then
		ui_print "! Disable $i !"
		touch /data/adb/modules/$i/disable
	fi
done


PIFBASE=/data/adb/modules/yapif/
# Keep old config files, use absolute path here for established installation.
F=$(cat $PIFBASE/yapif.ini | grep ^PROP_FILES | cut -d '=' -f2)
IFS=", "

for i in $F yapif.ini; do
	if [ -f $PIFBASE/$i ]; then
		ui_print "- Keeping $i"
		cp -af $PIFBASE/$i $MODPATH/$i
	fi
done

#keep only $ABI.so
for i in $MODPATH/zygisk/*.so; do
    if [[ ! $(basename $i) == $ABI.so ]]; then
        rm -f $i
    fi
done

ui_print " "
ui_print "Prepare sysprop"
mv -f $MODPATH/bin/$ABI/sysprop $MODPATH/
rm -rf $MODPATH/bin
set_perm $MODPATH/sysprop root root 755