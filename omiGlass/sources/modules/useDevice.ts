/**
 * useDevice Hook - 蓝牙设备连接管理
 *
 * 自定义React Hook,负责管理与OMI Glass智能眼镜的蓝牙连接
 * 功能:
 * 1. 连接蓝牙设备
 * 2. 自动重连机制
 * 3. 设备ID持久化存储
 */
import * as React from 'react';

// 本地存储键名:用于保存设备ID
const DEVICE_STORAGE_KEY = 'openglassDeviceId';

/**
 * useDevice - 蓝牙设备连接Hook
 *
 * @returns {[BluetoothRemoteGATTServer | null, () => Promise<void>, boolean]}
 *   - device: 已连接的蓝牙GATT服务器对象,未连接时为null
 *   - doConnect: 连接设备的函数
 *   - isAutoConnecting: 是否正在自动重连
 */
export function useDevice(): [BluetoothRemoteGATTServer | null, () => Promise<void>, boolean] {

    // 设备引用:用于在回调函数中访问最新的设备对象
    let deviceRef = React.useRef<BluetoothRemoteGATTServer | null>(null);

    // 设备状态:用于触发React组件重新渲染
    let [device, setDevice] = React.useState<BluetoothRemoteGATTServer | null>(null);

    // 自动重连状态标志
    let [isAutoConnecting, setIsAutoConnecting] = React.useState<boolean>(false);

    /**
     * 设置断开连接处理器
     *
     * @param {BluetoothDevice} connectedDevice - 已连接的蓝牙设备
     *
     * 当设备断开连接时,自动尝试重新连接
     */
    const setupDisconnectHandler = (connectedDevice: BluetoothDevice) => {
        connectedDevice.ongattserverdisconnected = async () => {
            console.log('Device disconnected, attempting to reconnect...');

            // 尝试重新连接
            setIsAutoConnecting(true);
            try {
                if (connectedDevice.gatt) {
                    const gatt = await connectedDevice.gatt.connect();
                    deviceRef.current = gatt;
                    setDevice(gatt);
                    console.log('Reconnection successful!');
                }
            } catch (err) {
                console.error('Reconnection failed:', err);
                deviceRef.current = null;
                setDevice(null);
            } finally {
                setIsAutoConnecting(false);
            }
        };
    };

    /**
     * 连接蓝牙设备
     *
     * 功能说明:
     * 1. 调用Web Bluetooth API请求设备
     * 2. 过滤设备名称为'OMI Glass'
     * 3. 连接GATT服务器
     * 4. 将设备ID保存到本地存储
     * 5. 设置断开连接处理器以支持自动重连
     */
    const doConnect = React.useCallback(async () => {
        try {
            // 请求蓝牙设备连接
            console.log('Requesting device connection...');
            let connected = await navigator.bluetooth.requestDevice({
                filters: [{ name: 'OMI Glass' }], // 过滤设备名称
                optionalServices: ['19B10000-E8F2-537E-4F6C-D104768A1214'.toLowerCase()], // OMI服务UUID
            });

            // 将设备ID保存到本地存储,用于未来的自动重连
            console.log('Storing device ID:', connected.id);
            localStorage.setItem(DEVICE_STORAGE_KEY, connected.id);

            // 连接到GATT服务器
            console.log('Connecting to GATT server...');
            let gatt: BluetoothRemoteGATTServer = await connected.gatt!.connect();
            console.log('Connected successfully!');

            // 更新状态
            deviceRef.current = gatt;
            setDevice(gatt);

            // 设置断开连接处理器,启用自动重连
            setupDisconnectHandler(connected);

        } catch (e) {
            // 处理连接失败错误
            console.error('Connection failed:', e);
        }
    }, []);

    // 返回设备对象、连接函数和自动重连状态
    return [device, doConnect, isAutoConnecting];
}
