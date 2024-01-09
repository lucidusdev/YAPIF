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


# Remove various 3rd party apps, these will restore upon removal YAPIF.
APPS="/product/app/XiaomiEUInject /system/app/XInjectModule /system/app/EliteDevelopmentModule"

for i in $APPS; do
	if [ -d $i ]; then
		app=$(echo $i | cut -d '/' -f4)
		directory="$MODPATH$i"
		[ -d "$directory" ] || mkdir -p "$directory"
		touch "$directory/.replace"
		ui_print "- Remove $app at $i"
	fi
done

# Keep old config files
PIFBASE=/data/adb/modules/yapif/
F=$(cat $PIFBASE/yapif.ini | grep PROP_FILES | cut -d '=' -f2 | sed 's/,/ /g')

for i in $F yapif.ini; do
	if [ -f $PIFBASE/$i ]; then
		ui_print "- Keeping $i"
		cp -af $PIFBASE/$i $MODPATH/$i
	fi
done


ui_print "- Prepare system_properties"
mv -f $MODPATH/bin/$ABI/system_properties $MODPATH/
rm -rf $MODPATH/bin
set_perm $MODPATH/system_properties root root 755