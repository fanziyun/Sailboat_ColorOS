cmd_/mnt/h/Sailboat_ColorOS/tools/hello/hello.mod := printf '%s\n'   hello.o | awk '!x[$$0]++ { print("/mnt/h/Sailboat_ColorOS/tools/hello/"$$0) }' > /mnt/h/Sailboat_ColorOS/tools/hello/hello.mod
