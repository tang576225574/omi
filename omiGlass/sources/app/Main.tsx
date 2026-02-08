/**
 * 主界面组件
 *
 * 负责管理与OMI Glass智能眼镜的蓝牙连接,并根据连接状态显示相应的界面
 */
import * as React from 'react';
import { SafeAreaView, StyleSheet, View, Text } from 'react-native';
import { RoundButton } from './components/RoundButton';
import { Theme } from './components/theme';
import { useDevice } from '../modules/useDevice';
import { DeviceView } from './DeviceView';
import { startAudio } from '../modules/openai';

/**
 * Main - 主界面组件
 *
 * 显示连接按钮和设备视图,管理蓝牙连接状态
 *
 * @returns {JSX.Element} 主界面
 */
export const Main = React.memo(() => {

    // device: 已连接的蓝牙设备(BluetoothRemoteGATTServer对象)
    // connectDevice: 连接设备的函数
    // isAutoConnecting: 是否正在自动重连
    const [device, connectDevice, isAutoConnecting] = useDevice();

    // 本地连接状态标志
    const [isConnecting, setIsConnecting] = React.useState(false);

    /**
     * 处理用户点击连接按钮
     *
     * 调用connectDevice函数尝试连接蓝牙设备
     */
    const handleConnect = React.useCallback(async () => {
        setIsConnecting(true);
        try {
            await connectDevice();
        } finally {
            setIsConnecting(false);
        }
    }, [connectDevice]);

    return (
        <SafeAreaView style={styles.container}>
            {/* 未连接状态: 显示连接按钮 */}
            {!device && (
                <View style={{ flex: 1, justifyContent: 'center', alignItems: 'center', alignSelf: 'center' }}>
                    {isConnecting ? (
                        <Text style={styles.statusText}>Connecting to OpenGlass...</Text>
                    ) : (
                        <RoundButton title="Connect to the device" action={handleConnect} />
                    )}
                </View>
            )}
            {/* 已连接状态: 显示设备视图 */}
            {device && (
                <DeviceView device={device} />
            )}
        </SafeAreaView>
    );
});

const styles = StyleSheet.create({
    container: {
        flex: 1,
        backgroundColor: Theme.background,
        alignItems: 'stretch',
        justifyContent: 'center',
    },
    statusText: {
        color: Theme.text,
        fontSize: 18,
        marginBottom: 16,
    }
});