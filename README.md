# 开发者1 : 大肥鱼（DeekSeek）
  开发者2 : 大肥龙 （GPT6 astra)
  我负责阿巴阿巴


# ug

NX 插件工程生成器。扫描本机工具链，交互式生成可直接构建的插件工程。

## 构建

```powershell
cmake -S . -B build
cmake --build build --config Release
```

产物：`bin/ug.exe`。

## 使用

```powershell
ug new
```

生成完成后：

```powershell
cd <项目目录>
cmake --preset nx
cmake --build --preset nx-release
```

`ug --help` 查看全部命令。

## 许可证

MIT
