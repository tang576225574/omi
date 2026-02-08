/**
 * Agent类 - AI代理核心类
 *
 * 负责处理从智能眼镜接收的照片,使用AI模型生成图像描述,并回答用户问题
 * 主要功能:
 * 1. 接收并存储照片
 * 2. 为每张照片生成AI描述
 * 3. 基于图片描述回答用户问题
 * 4. 管理状态更新和UI通知
 */
import * as React from 'react';
import { AsyncLock } from "../utils/lock";
import { imageDescription, llamaFind } from "./imageDescription";
import { startAudio } from '../modules/openai';

/**
 * AgentState - 代理状态接口
 *
 * @property {string} lastDescription - 最后一张照片的描述
 * @property {string} answer - AI回答的结果
 * @property {boolean} loading - 是否正在处理请求
 */
type AgentState = {
    lastDescription?: string;
    answer?: string;
    loading: boolean;
}

/**
 * Agent类 - AI代理
 *
 * 使用异步锁保证操作的线程安全,维护照片队列和状态
 */
export class Agent {
    // 异步锁,确保照片处理和问答操作的互斥
    #lock = new AsyncLock();

    // 照片队列:存储照片数据和对应的AI描述
    #photos: { photo: Uint8Array, description: string }[] = [];

    // 内部状态:存储最新的描述、回答和加载状态
    #state: AgentState = { loading: false };

    // 状态副本:用于React状态更新
    #stateCopy: AgentState = { loading: false };

    // 状态监听器列表:当状态更新时通知所有订阅者
    #stateListeners: (() => void)[] = [];

    /**
     * 添加照片并生成描述
     *
     * @param {Uint8Array[]} photos - 照片数据数组
     *
     * 功能说明:
     * 1. 使用异步锁确保串行处理
     * 2. 对每张照片调用AI模型生成描述
     * 3. 将照片和描述存储到队列中
     * 4. 更新UI状态显示最新描述
     */
    async addPhoto(photos: Uint8Array[]) {
        await this.#lock.inLock(async () => {

            // 遍历每张照片,生成描述
            let lastDescription: string | null = null;
            for (let p of photos) {
                console.log('Processing photo', p.length);
                // 调用AI模型生成图像描述
                let description = await imageDescription(p);
                console.log('Description', description);
                // 存储照片和描述
                this.#photos.push({ photo: p, description });
                lastDescription = description;
            }

            // TODO: 更新摘要信息(未来可以添加多张照片的综合摘要)

            // 更新UI:显示最新照片的描述
            if (lastDescription) {
                this.#state.lastDescription = lastDescription;
                this.#notify();
            }
        });
    }

    /**
     * 回答用户问题
     *
     * @param {string} question - 用户的问题
     *
     * 功能说明:
     * 1. 启动音频播放
     * 2. 汇总所有照片的描述
     * 3. 使用LLM模型基于图片描述回答问题
     * 4. 更新状态并通知UI
     */
    async answer(question: string) {
        // 尝试启动音频(用于语音播报)
        try {
            startAudio()
        } catch(error) {
            console.log("Failed to start audio")
        }

        // 如果正在加载中,直接返回
        if (this.#state.loading) {
            return;
        }

        // 设置加载状态
        this.#state.loading = true;
        this.#notify();

        await this.#lock.inLock(async () => {
            // 汇总所有照片的描述
            let combined = '';
            let i = 0;
            for (let p of this.#photos) {
                combined + '\n\nImage #' + i + '\n\n';
                combined += p.description;
                i++;
            }

            // 调用LLM模型回答问题
            let answer = await llamaFind(question, combined);

            // 更新状态
            this.#state.answer = answer;
            this.#state.loading = false;
            this.#notify();
        });
    }

    /**
     * 通知所有状态监听器
     *
     * 内部方法:当状态更新时调用,通知React组件重新渲染
     */
    #notify = () => {
        this.#stateCopy = { ...this.#state };
        for (let l of this.#stateListeners) {
            l();
        }
    }

    /**
     * React Hook - 订阅Agent状态
     *
     * @returns {AgentState} 当前状态
     *
     * 在React组件中使用此方法订阅Agent状态变化,
     * 当状态更新时组件会自动重新渲染
     */
    use() {
        const [state, setState] = React.useState(this.#stateCopy);
        React.useEffect(() => {
            const listener = () => setState(this.#stateCopy);
            this.#stateListeners.push(listener);
            return () => {
                this.#stateListeners = this.#stateListeners.filter(l => l !== listener);
            }
        }, []);
        return state;
    }
}