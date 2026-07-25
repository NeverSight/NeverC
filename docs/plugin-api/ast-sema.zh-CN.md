**语言**: [English](ast-sema.md) | [简体中文](ast-sema.zh-CN.md) | [繁體中文](ast-sema.zh-TW.md) | [日本語](ast-sema.ja.md) | [한국어](ast-sema.ko.md) | [Français](ast-sema.fr.md) | [Deutsch](ast-sema.de.md) | [Español](ast-sema.es.md) | [Italiano](ast-sema.it.md) | [Русский](ast-sema.ru.md) | [العربية](ast-sema.ar.md)

# AST、解析器与语义插件 API

`PluginAST.h` 和 `PluginSema.h` 提供任务作用域的纯 C 访问方式，用于操作前端语法树
和语义流水线。稳定的节点、属性和子槽 ID 由 NeverC 的具体 AST 定义生成；插件永远不会
拿到 C++ 的 `Decl`、`Stmt`、`Type` 或 `Sema` 指针。

## 读取与构建 AST 节点

使用 `NevercASTAPI` 查询节点信息、schema 属性、子节点、父节点、声明上下文、类型、
属性以及常见具体节点的细节。批量 API 要求显式给出元素数量、容量和步长。

`NevercASTBuilder` 只构造 schema 中声明过的节点种类。必需的属性和子槽会在提交时被
校验。提交成功会发布一个任务拥有的节点；提交失败则不会留下任何部分可见的节点。无论
提交成功还是失败，都要销毁每一个构建器。

## 原子变更

AST 变更通过 `BeginASTMutation`、暂存操作和 `CommitASTMutation` 完成。宿主会在改动
语法树之前校验归属、槽位兼容性、基数、父链接、环路和语义不变式。`AbortASTMutation`
会丢弃所有暂存操作。原生的 `TreeMutationListener` 通知只在提交成功之后才发出。

可构建的 [`ASTRewritePlugin.c`](../../pluginsdk/examples/ASTRewritePlugin.c)
展示了一个解析器拦截器：它调用内建解析器，构造一个整数字面量，然后原子地替换变量
初始化器。

## 替换解析器与 Sema

`neverc.syntax.parse` 把已验证的 token 流映射为 `ASTUnit`。`neverc.sema.analyze`
把 AST 产物映射为 `SemanticUnit`。这两个阶段都具备类型化拦截器和 Provider。如果只
想替换前端的一部分，声明、语句、表达式、类型名、属性、查找、转换和关键字等细粒度
扩展阶段依然可用。

内建的融合式 parser/Sema 路径发布与替换实现完全相同的产物契约。语义重放只接受那些
NeverC 能够重建作用域、名字查找、重复声明和类型检查状态的节点种类。遇到不受支持的
具体种类时返回 `NEVERC_STATUS_UNSUPPORTED_AST_KIND`，绝不会把只重放了一部分的语法树
标记为语义完整。

## 生命周期与清理

AST 和 Sema 的生命周期观察者通过宿主的 `TreeConsumer` 桥接按源码顺序投递。即使发生
语法错误、插件错误或取消，begin/end 事件也保持成对。任务句柄只有在最终的只读 end
事件和清理回调执行完毕之后才会失效。

## 验证

```sh
python3 utils/plugin-api/gen-ast-schema.py --check
ctest --test-dir build-neverc \
  -R 'Plugin(AST|Parser|Sema|Frontend)' --output-on-failure
```

启用 `NEVERC_ENABLE_PLUGIN_FUZZERS=ON` 后，`plugin-ast-mutation-fuzzer` 覆盖属性
解码、畸形构建器、伪造句柄和变更回滚。
