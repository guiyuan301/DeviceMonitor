

https://github.com/user-attachments/assets/815dc51f-02b4-4d6d-8cd8-312e3cac390b


运行这个需要在开发板安装FFmpeg4.2.1
先在挂载目录下执行如下指令(板子)
cp /mnt/nfs/ffmpeg-4.2.1/_install/bin/ffmpeg /usr/bin/
chmod +x /usr/bin/ffmpeg

# ① 确认 v4l2 输入设备存在
ffmpeg -devices 2>/dev/null | grep -i video4linux

# ② 端到端实测你的摄像头 (video1)
ffmpeg -f v4l2 -input_format mjpeg -i /dev/video1 -f image2pipe -vcodec copy - | head -c 64 | xxd
启动客服端 需要先运行.sh脚本(避免无法使用触摸屏)

# 看每个节点的身份, 带 "Metadata" 字样的就是元数据节点, 不能用
cat /sys/class/video4linux/video*/name

# 试 video0 (如果 HMI 下拉框里本来就列了 /dev/video0, 直接选它就能看画面)
ffmpeg -f v4l2 -input_format mjpeg -i /dev/video0 -f image2pipe -vcodec copy - | head -c 64 | xxd
直接运行一下脚本即可 
cat > /root/run_hmi.sh << 'EOF'
#!/bin/sh
# ============================================================
# 车间设备监控系统 HMI 一键启动（最终版 · 自适应 + 防吞点击）
# 自动：找程序 / 找触摸屏 / 找tslib / 找Qt插件
#       重写ts.conf(去滤波) / 恢复udev规则(禁evdevtouch)
# 用法：/root/run_hmi.sh
# ============================================================
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

# ---------- 2. 自动找触摸屏设备节点（Goodix/含touch关键字） ----------
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

# ---------- 5. 重写 ts.conf：只留 input 转单点（ dejitter 会吞单击！ ） ----------
if [ ! -f /etc/ts.conf.factory ]; then
    cp /etc/ts.conf /etc/ts.conf.factory 2>/dev/null
fi
if ! grep -q "mt_missing" /etc/ts.conf 2>/dev/null; then
    echo "module_raw input mt_missing=1" > /etc/ts.conf
    echo "[OK] 已重写 /etc/ts.conf（去除滤波，防吞点击）"
fi

# ---------- 6. 恢复 udev 规则：禁用 evdevtouch（新刷机必补） ----------
RULE=/etc/udev/rules.d/99-qt-tslib-only.rules
if [ ! -f "$RULE" ]; then
    echo 'SUBSYSTEM=="input", ATTRS{name}=="Goodix*", ENV{ID_INPUT_TOUCHSCREEN}=""' > "$RULE"
    udevadm control --reload-rules 2>/dev/null
    udevadm trigger 2>/dev/null
    echo "[OK] 已恢复 udev 规则（禁用 evdevtouch 抢触摸）"
fi

# ---------- 7. 环境变量 ----------
export TSLIB_TSDEVICE=$TSDEV
export TSLIB_CONFFILE=/etc/ts.conf
[ -n "$TSPLUG" ] && export TSLIB_PLUGINDIR=$TSPLUG
export TSLIB_FBDEVICE=/dev/fb0
[ -n "$QTPLUG" ] && export QT_PLUGIN_PATH=$QTPLUG
export QT_QPA_PLATFORM=linuxfb:/dev/fb0

# ---------- 8. 杀掉抢屏/抢触摸进程 ----------
killall App 2>/dev/null
killall HMI 2>/dev/null
sleep 1

# ---------- 9. 启动 ----------
echo "[OK] 启动 /root/HMI ..."
exec /root/HMI -platform linuxfb -plugin tslib
EOF
chmod +x /root/run_hmi.sh
/root/run_hmi.sh
