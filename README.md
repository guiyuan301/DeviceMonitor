<img width="1280" height="720" alt="0a58518f1bdd4280621b08790411d07e_720" src="https://github.com/user-attachments/assets/fef36a50-a6d8-4143-b84b-4c291647fafe" />
启动客服端 需要先运行.sh脚本(避免无法使用触摸屏)
cat > /root/run_hmi.sh << 'EOF'
#!/bin/sh


直接运行一下脚本即可 
cat > /root/run_hmi.sh << 'EOF'
#!/bin/sh
# ============================================
# 车间设备监控系统 HMI 一键启动（自适应版）
# 自动：找程序 / 找触摸屏 / 找tslib / 找Qt插件 / 补ts.conf
# ============================================
echo "=========================================="
echo " 车间设备监控系统 HMI 一键启动"
echo "=========================================="

# ---------- 1. 找 HMI 程序，统一放到 /root/HMI ----------
if [ ! -x /root/HMI ]; then
    for SRC in /mnt/nfs/HMI /mnt/nfs/WorkshopHMI /root/WorkshopHMI ./HMI ./WorkshopHMI; do
        if [ -x "$SRC" ]; then
            cp -f "$SRC" /root/HMI
            echo "[OK] 已从 $SRC 复制到 /root/HMI"
            break
        fi
    done
fi
if [ ! -x /root/HMI ]; then
    echo "[失败] 板子上找不到 HMI 程序！请先在 Windows 上传："
    echo "  scp WorkshopHMI root@板子IP:/mnt/nfs/"
    exit 1
fi

# ---------- 2. 自动找触摸屏设备节点（Goodix/含touch关键字的） ----------
TSDEV=""
for d in /sys/class/input/event*/device; do
    if cat "$d/name" 2>/dev/null | grep -qi "goodix\|touch\|gt9"; then
        TSDEV=/dev/input/$(basename $(dirname "$d"))
        break
    fi
done
if [ -z "$TSDEV" ]; then
    EV=$(grep -iA4 "touch" /proc/bus/input/devices | grep -o "event[0-9]*" | head -1)
    [ -n "$EV" ] && TSDEV=/dev/input/$EV
fi
[ -z "$TSDEV" ] && TSDEV=/dev/input/event2
echo "[OK] 触摸屏设备: $TSDEV"

# ---------- 3. 自动找 tslib 插件目录 ----------
if [ -d /usr/lib/arm-linux-gnueabihf/ts0 ]; then
    TSPLUG=/usr/lib/arm-linux-gnueabihf/ts0
elif [ -d /usr/lib/ts ]; then
    TSPLUG=/usr/lib/ts
else
    TSPLUG=""
    echo "[警告] 找不到 tslib 插件目录，触摸可能没反应"
fi
echo "[OK] tslib 插件: ${TSPLUG:-无}"

# ---------- 4. 自动找 Qt 插件目录 ----------
if [ -d /usr/lib/plugins ]; then
    QTPLUG=/usr/lib/plugins
elif [ -d /usr/lib/qt5/plugins ]; then
    QTPLUG=/usr/lib/qt5/plugins
else
    QTPLUG=""
    echo "[警告] 找不到 Qt 插件目录"
fi
echo "[OK] Qt 插件: ${QTPLUG:-无}"

# ---------- 5. ts.conf 没有 input 模块就自动补（Type-B转单点） ----------
if [ -f /etc/ts.conf ] && ! grep -q "module_raw input" /etc/ts.conf; then
    cp /etc/ts.conf /etc/ts.conf.bak 2>/dev/null
    echo "module_raw input mt_missing=1" >> /etc/ts.conf
    echo "[OK] 已自动补充 /etc/ts.conf 触摸模块"
fi

# ---------- 6. 环境变量 ----------
export TSLIB_TSDEVICE=$TSDEV
export TSLIB_CONFFILE=/etc/ts.conf
[ -n "$TSPLUG" ] && export TSLIB_PLUGINDIR=$TSPLUG
export TSLIB_FBDEVICE=/dev/fb0
[ -n "$QTPLUG" ] && export QT_PLUGIN_PATH=$QTPLUG
export QT_QPA_PLATFORM=linuxfb:/dev/fb0

# ---------- 7. 杀掉抢屏/抢触摸进程 ----------
killall App 2>/dev/null
killall HMI 2>/dev/null
sleep 1

# ---------- 8. 启动 ----------
echo "[OK] 启动 /root/HMI ..."
exec /root/HMI -platform linuxfb -plugin tslib
EOF
chmod +x /root/run_hmi.sh
/root/run_hmi.sh
