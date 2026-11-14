cmd_/mnt/h/Sailboat_ColorOS/tools/hello/modules.order := {   echo /mnt/h/Sailboat_ColorOS/tools/hello/hello.ko; :; } | awk '!x[$$0]++' - > /mnt/h/Sailboat_ColorOS/tools/hello/modules.order
