**语言**: [English](prep.md) | [简体中文](prep.zh-CN.md) | [繁體中文](prep.zh-TW.md) | [日本語](prep.ja.md) | [한국어](prep.ko.md) | [Français](prep.fr.md) | [Deutsch](prep.de.md) | [Español](prep.es.md) | [Italiano](prep.it.md) | [Русский](prep.ru.md) | [العربية](prep.ar.md)

# 预处理器插件 API

`PluginPrep.h` 暴露稳定的 token、标识符、宏、pragma 和 token 流 schema，且不泄露
NeverC 或 LLVM 的 C++ 类型。生成的 schema `Schema/PluginPrepSchema.inc` 是稳定数值
种类、类别、拼写和可构造性的唯一权威来源。

## 扩展层次

插件可以在三个层次上参与：

- 针对 include、宏展开、条件编译、pragma 和文件切换的只读预处理器事件；
- 针对 token、include、宏、pragma 和特性查询各阶段的类型化拦截器；
- 一个完整的 `neverc.prep.build_token_stream` Provider，用于发布经过验证的
  `TokenStream`。

token 阶段支持有界的替换、删除和展开。宿主会强制执行展开预算，并在发布替换结果前
校验拼写、位置、标志、EOF 位置以及 token 归属。

## Token 构建器

用 `CreateTokenBuilder` 创建合成 token，设置且仅设置一份 token 载荷，赋予一个有效
的、任务拥有的位置，然后调用 `TokenBuilderCommit`。在每条路径上都要销毁构建器。已
提交的构建器不可变，提交失败不会发布任何 token。

Token 流是连续的、不可变的任务产物。替换流必须恰好包含一个位于末尾的 EOF token，且
不得超过 `NEVERC_PREP_TOKEN_STREAM_MAX_TOKENS`。

## 观察者与拦截器规则

观察者接收只读事件数据，不能影响预处理过程。拦截器遵循通用的续延契约：

- 最多调用一次 `InvokeNext`，然后返回 `CONTINUE`；或者
- 不调用它，而是发布一个经过验证的替换结果。

续延对象以及所有预处理器句柄只在其声明的回调/任务作用域内有效。插件创建的线程如果
触及这些值，必须在回调返回前完成 join。

## 验证

修改 token 定义后，运行生成 schema 与覆盖率检查：

```sh
python3 utils/plugin-api/gen-prep-schema.py --check
python3 utils/plugin-api/check-coverage.py docs/plugin-api/coverage.json
```

启用 `NEVERC_ENABLE_PLUGIN_FUZZERS=ON` 后，
`plugin-prep-token-builder-fuzzer` 会针对畸形 token 构建器、任务句柄、输出容量和
token 流查询进行测试。
