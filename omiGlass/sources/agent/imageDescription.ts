/**
 * 图像描述和问答模块
 *
 * 提供AI图像识别和基于图像描述的问答功能
 * 支持多种AI模型:Ollama(本地)、Groq、OpenAI
 */
import { KnownModel, ollamaInference } from "../modules/ollama";
import { groqRequest } from "../modules/groq-llama3";
import { gptRequest } from "../modules/openai";

/**
 * imageDescription - 生成图像描述
 *
 * @param {Uint8Array} src - 图像数据(JPEG格式)
 * @param {KnownModel} model - 使用的AI模型,默认为moondream:1.8b-v2-fp16
 * @returns {Promise<string>} 图像描述文本
 *
 * 使用本地Ollama模型(moondream)对图像进行分析,
 * 生成详细的场景描述,并识别图像中的文字
 */
export async function imageDescription(src: Uint8Array, model: KnownModel = 'moondream:1.8b-v2-fp16'): Promise<string> {
    return ollamaInference({
        model: model,
        messages: [{
            role: 'system',
            content: 'You are a very advanced model and your task is to describe the image as precisely as possible. Transcribe any text you see.'
        }, {
            role: 'user',
            content: 'Describe the scene',
            images: [src],
        }]
    });
}

/**
 * llamaFind - 使用Groq/Llama模型回答问题
 *
 * @param {string} question - 用户的问题
 * @param {string} images - 图像描述文本(可包含多张图片的描述)
 * @returns {Promise<string>} AI生成的答案
 *
 * 基于图像描述文本,使用Groq API(Llama模型)回答用户问题
 * 特点:
 * - 只使用图像描述中的信息回答
 * - 不推测或泛化场景
 * - 答案简洁具体
 */
export async function llamaFind(question: string, images: string): Promise<string> {
    return groqRequest(
             `
                You are a smart AI that need to read through description of a images and answer user's questions.

                This are the provided images:
                ${images}

                DO NOT mention the images, scenes or descriptions in your answer, just answer the question.
                DO NOT try to generalize or provide possible scenarios.
                ONLY use the information in the description of the images to answer the question.
                BE concise and specific.
            `
        ,
            question
    );
}

/**
 * openAIFind - 使用OpenAI GPT模型回答问题
 *
 * @param {string} question - 用户的问题
 * @param {string} images - 图像描述文本
 * @returns {Promise<string>} AI生成的答案
 *
 * 功能与llamaFind类似,但使用OpenAI的GPT模型
 * 可根据需要切换使用不同的AI后端
 */
export async function openAIFind(question: string, images: string): Promise<string> {
    return gptRequest(
             `
                You are a smart AI that need to read through description of a images and answer user's questions.

                This are the provided images:
                ${images}

                DO NOT mention the images, scenes or descriptions in your answer, just answer the question.
                DO NOT try to generalize or provide possible scenarios.
                ONLY use the information in the description of the images to answer the question.
                BE concise and specific.
            `
        ,
            question
    );
}