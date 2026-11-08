#pragma once

#include <QObject>
#include <QPoint>

// 全局低级鼠标钩子，用于 Super-snip（按住 Win + 拖拽直接截图）。
//
// 约束：WH_MOUSE_LL 回调必须 < 30ms 返回，否则 Windows 会静默移除钩子。
// 所以回调里只记录坐标，实际工作通过信号排队到主线程。
class MouseHook : public QObject
{
    Q_OBJECT

public:
    explicit MouseHook(QObject *parent = nullptr);
    ~MouseHook() override;

    // 开始 / 停止监听。失败（被安全软件拦截等）返回 false。
    bool install();
    void uninstall();
    bool isInstalled() const { return m_installed; }

signals:
    // 一次完整的 Win+左键拖拽结束，给出起止点（屏幕逻辑坐标）
    void superSnipFinished(const QPoint &from, const QPoint &to);

private:
    // 回调签名对齐 HOOKPROC：LRESULT __stdcall(int, WPARAM, LPARAM)
    static long long __stdcall hookProc(int code, unsigned long long wParam,
                                        long long lParam);

    static MouseHook *s_instance;

    void *m_hook = nullptr;
    bool  m_installed = false;

    bool   m_dragging = false;
    QPoint m_start;
};
