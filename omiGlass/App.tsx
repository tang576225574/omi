/**
 * 应用程序根组件
 *
 * 这是Expo应用的入口文件,负责渲染主界面组件
 */
import * as React from 'react';
// import App from './sources/app';
import { Main } from './sources/app/Main';

/**
 * Root - 应用根组件
 *
 * @returns {JSX.Element} 主界面组件
 */
export default function Root() {
  return <Main />;
}