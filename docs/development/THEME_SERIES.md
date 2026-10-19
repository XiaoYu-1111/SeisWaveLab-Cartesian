# SeisWave Lab 系列主题

曲网格 `src/common/ui/Presentation3D.cpp` 是视觉标准。本项目 AppTheme 保持相同的十种主题颜色、选择顺序、控件间距、面板底色、按钮、标签栏及功能色混合规则。

顺序：Paper、Arctic、Soft Jade、CUDA Green、Ocean Blue、Graphite、Scientific Light、Midnight Violet、Teal Studio、Warm Ivory。

旧主题编号保持兼容，原 Amber Lab 在旧配置迁移为 Warm Ivory；后续保存采用 SeisUiTheme2。保留独立主题选择，不读取或修改曲网格软件设置。共享的是界面配色，波场与物性科学色带不由主题切换重置。

新增或更新主题时，应对照曲网格同步 AppTheme.cpp、翻译与主题回归检查。
