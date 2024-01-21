# Yet Another Play Integrity Fix

This is just another PIF module based on [chiteroman's PIF module](https://github.com/chiteroman/PlayIntegrityFix) & [osm0sis's Fork](https://github.com/osm0sis/PlayIntegrityFork), with:

- supports both `.prop` and `.json` fingerprints, requiring fewer fields.
- lots of customization options in `yapif.ini` settings.
- optional [system_properties](https://github.com/lucidusdev/android_properties) binary for setting system properties and avoid detection.

For general information about PIF modules, including this one, and how to find/use fingerprint file, please check the excellent guide on [osm0sis's PIF Fork](https://github.com/osm0sis/PlayIntegrityFork/blob/main/README.md). 



## Special Features in YAPIF

### Customized PIF prop lists(PROP_FILES in yapif.ini)

Users can set their own fingerprint files list, separated by ,(comma). The file can be in either `json` or java `prop` format and the first available one will be used.

This also supports **absolute path**, e.g.`/sdcard/Download/pif.json`, this allows other PIF update utility to run without root, or modifying the prop file with ease.



### Auto fill missing property fields(AUTOPROP)

A fingerprint string, as defined in `ro.build.fingerprint`, can be composed from various other fields as shown in [Android source code](https://android.googlesource.com/platform/frameworks/base/+/master/core/java/android/os/Build.java#1248). Theoretically, 8 properties can be derived from a single fingerprint string.

For example, from

```google/taimen/taimen:10/QQ2A.200501.001.B3/6396602:user/release-keys```

 we can get below field/value.

| Field Name  | Value              |
| ----------- | ------------------ |
| BRAND       | google             |
| NAME        | taimen             |
| DEVICE      | taimen             |
| RELEASE     | 10                 |
| ID          | QQ2A.200501.001.B3 |
| INCREMENTAL | 6396602            |
| TYPE        | user               |
| TAGS        | release-keys       |

So the only fields missing are `MANUFACTURER` `MODEL` `SECURITY_PATCH` `FIRST_API_LEVEL`(or whatever name), and a fingerprint prop file can include as few as 5 fields -- all 4 above plus `FINGERPRINT`.

There're exceptions though, i.e., BRAND/NAME/DEVICE etc. may be different from derived ones, set them if necessary and YAPIF doesn't override user defined values.



### Map Java properties to system properties(PROPMAP1/PROPMAP2)

Most Java properties, with name in upper case, are actually used in native code as well. For example, with `ID=QQ2A.200501.001.B3`, both `ID` field in Java and `ro.build.id` property in native code are set to `QQ2A.200501.001.B3`, user only needs to define `ID=QQ2A.200501.001.B3`  and YAPIF will set both. YAPIF can even set multiple property patterns based on same Java prop.

The mapping schema is (pre)defined in `PROPMAP1` and `PROPMAP2` section in `yapif.ini` for flexibility.



### User defined properties(USERPROP)

Use this to set additional property which applies to all pif.prop. Support both Java and system property(including wildcard match). By default, these get a lower priority and won't override user defined value in pif.prop. Add `!`(exclamation mark) at the beginning to override.

Example

```c
//this will set DEVICE_INITIAL_SDK_INT to 24 when it's missing from 'pif.prop'
DEVICE_INITIAL_SDK_INT=24
//this will set DEVICE_INITIAL_SDK_INT to 24 regardless 'pif.prop'
!DEVICE_INITIAL_SDK_INT=24
```



### Spoof required properties inside YAPIF(SPOOFPROP)

To pass Play Integrity, some "read-only" system properties have to be modified by Magisk `resetprop` utility in `service.sh` and `post-fs-data.sh` . With `SPOOFPROP=1`, majority of changes are done(spoofed) inside YAPIF instead of `resetprop` in boot scripts. This allows minimal system modification but may expose unlocked bootloader status to other apps. 



### Stealth `resetprop` replacement(SYSPROP=1)

When `resetprop` changing a property, some traces are left and can be used for root detection, as discussed in [XDA](https://xdaforums.com/t/module-play-integrity-fix-safetynet-fix.4607985/page-448#post-89260617). First one is already addressed in Magisk canary but not yet in stable 26400. [Second issue](https://github.com/topjohnwu/Magisk/issues/7696), however, are not yet fixed.

Some remedies are available, including Shamiko module. Beside that, osm0sis developed a fully portable shell script based hex patch, in YAPIF, a [`system_properties`](https://github.com/lucidusdev/android_properties) binary is included. This should make PIF work better with some bank apps. Set `SYSPROP=1` in `yapif.ini` to use `system_properties` instead of `resetprop`.



## Known Issue

- Limited support for json format. json file must have each key/value pair in a separated line:

  ```javascript
  { 
  	//this is a working sample
  	PROP1: "value1",
  	PROP2: "value2",
  }
  ```
  
  Below wouldn't work.
  
  ```javascript
  {PROP1: "value1",PROP2: "value2"}
  ```
  
  ```javascript
  { 	PROP1: "value1",
  	PROP2: "value2"
  }
  ```
  
  

For other limitations, please check [comments in source code](https://github.com/lucidusdev/YAPIF/blob/dev/app/src/main/cpp/pifprop.hpp).





















