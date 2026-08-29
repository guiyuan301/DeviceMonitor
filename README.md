<img width="1280" height="720" alt="0a58518f1bdd4280621b08790411d07e_720" src="https://github.com/user-attachments/assets/fef36a50-a6d8-4143-b84b-4c291647fafe" />
启动客服端 需要先运行.sh脚本(避免无法使用触摸屏)
cat > /root/run_hmi.sh << 'EOF'
#!/bin/sh

# ===== 车间设备监控 HMI 启动脚本（最终版） =====
export TSLIB_TSDEVICE=/dev/input/event2 #这里的event2可能每个人都不同 
export TSLIB_CONFFILE=/etc/ts.conf
export TSLIB_PLUGINDIR=/usr/lib/arm-linux-gnueabihf/ts0
export TSLIB_FBDEVICE=/dev/fb0
export QT_PLUGIN_PATH=/usr/lib/plugins
export QT_QPA_PLATFORM=linuxfb:/dev/fb0
killall App 2>/dev/null
killall HMI 2>/dev/null
exec /root/HMI -platform linuxfb -plugin tslib
EOF
chmod +x /root/run_hmi.sh

grep -A6 'Goodix' /proc/bus/input/device #查看event(?)
od -x /dev/input/event(?) #手指点击屏幕有输出就对

#运行
./WorkshopHMI -platform linuxfb -plugin tslib
