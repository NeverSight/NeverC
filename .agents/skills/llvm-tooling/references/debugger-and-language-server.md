# Debugger And Language Server

## LLDB Extension Development

### Python Commands
```python
import lldb

def my_command(debugger, command, result, internal_dict):
    """Custom LLDB command implementation"""
    target = debugger.GetSelectedTarget()
    process = target.GetProcess()
    thread = process.GetSelectedThread()
    frame = thread.GetSelectedFrame()

    # Get variable value
    var = frame.FindVariable("my_var")
    result.AppendMessage(f"my_var = {var.GetValue()}")

def __lldb_init_module(debugger, internal_dict):
    debugger.HandleCommand(
        'command script add -f my_module.my_command my_cmd')
```

### Scripted Process
```python
class MyScriptedProcess(lldb.SBScriptedProcess):
    def __init__(self, target, args):
        super().__init__(target, args)

    def get_thread_with_id(self, tid):
        # Return thread info
        pass

    def get_registers_for_thread(self, tid):
        # Return register context
        pass

    def read_memory_at_address(self, addr, size):
        # Read memory
        pass
```

### LLDB GUI Extensions
- **visual-lldb**: GUI frontend for LLDB
- **vegvisir**: Browser-based LLDB GUI
- **lldbg**: Lightweight native GUI
- **CodeLLDB**: VSCode debugger extension

## Clangd / Language Server Protocol

### Custom LSP Extensions
```cpp
// Implementing custom code actions
class MyCodeActionProvider {
public:
    std::vector<CodeAction> getCodeActions(
        const TextDocumentIdentifier &File,
        const Range &Range,
        const CodeActionContext &Context) {

        std::vector<CodeAction> Actions;

        // Add custom refactoring action
        CodeAction Action;
        Action.title = "Extract to function";
        Action.kind = "refactor.extract";

        // Define workspace edits
        WorkspaceEdit Edit;
        // ... populate edits
        Action.edit = Edit;

        Actions.push_back(Action);
        return Actions;
    }
};
```

### Clangd Configuration
```yaml
# .clangd configuration file
CompileFlags:
  Add: [-xc++, -std=c++17]
  Remove: [-W*]

Diagnostics:
  ClangTidy:
    Add: [modernize-*, performance-*]
    Remove: [modernize-use-trailing-return-type]

Index:
  Background: Build
```
