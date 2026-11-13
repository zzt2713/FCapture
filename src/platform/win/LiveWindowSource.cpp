#include "platform/win/LiveWindowSource.h"

#include "platform/win/WinUtil.h"

#include <QDebug>
#include <QTimer>

#include <d3d11.h>
#include <windows.h>
#include <wrl/client.h>

#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Graphics.Capture.h>
#include <winrt/Windows.Graphics.DirectX.h>
#include <winrt/Windows.Graphics.DirectX.Direct3D11.h>
#include <winrt/Windows.Graphics.h>

#include <windows.graphics.capture.interop.h>
#include <windows.graphics.directx.direct3d11.interop.h>

#include <QDateTime>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstring>
#include <mutex>

using Microsoft::WRL::ComPtr;
using namespace winrt;
using namespace winrt::Windows::Graphics::Capture;
using namespace winrt::Windows::Graphics::DirectX;
using namespace winrt::Windows::Graphics::DirectX::Direct3D11;

namespace {

// 每帧回读的节奏。snow 用 32ms（约 30fps）；这里跟它一致，
// 既能跟上窗口滚动，又不会把 CPU 吃满。
constexpr int kFrameIntervalMs = 32;

// 帧池缓冲数（snow 的 WGC_FRAME_POOL_BUFFERS = 3）。
// 2 个缓冲在窗口快速重绘时容易丢帧。
constexpr int kPoolBuffers = 3;

// 一次回调最多榨取的帧数，防止极端情况下死循环。
constexpr int kMaxDrainPerCallback = 16;

// 创建 IDirect3DDevice（WGC 需要 D3D11 设备）
ComPtr<ID3D11Device> createD3DDevice()
{
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    D3D_FEATURE_LEVEL levels[] = { D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0,
                                   D3D_FEATURE_LEVEL_10_1, D3D_FEATURE_LEVEL_10_0 };
    UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
    const HRESULT hr = ::D3D11CreateDevice(
        nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags, levels,
        UINT(std::size(levels)), D3D11_SDK_VERSION, device.GetAddressOf(),
        nullptr, context.GetAddressOf());
    if (FAILED(hr))
        return nullptr;
    return device;
}

// ID3D11Device → IDirect3DDevice（WinRT 包装）
winrt::Windows::Graphics::DirectX::Direct3D11::IDirect3DDevice
wrapD3DDevice(const ComPtr<ID3D11Device> &device)
{
    ComPtr<IDXGIDevice> dxgi;
    if (FAILED(device.As(&dxgi)))
        return nullptr;

    winrt::com_ptr<IInspectable> inspectable;
    const HRESULT hr = ::CreateDirect3D11DeviceFromDXGIDevice(dxgi.Get(), inspectable.put());
    if (FAILED(hr))
        return nullptr;
    return inspectable.as<winrt::Windows::Graphics::DirectX::Direct3D11::IDirect3DDevice>();
}

// 从 HWND 取 GraphicsCaptureItem
GraphicsCaptureItem createCaptureItem(quintptr windowId)
{
    const auto interop =
        winrt::get_activation_factory<GraphicsCaptureItem, IGraphicsCaptureItemInterop>();
    GraphicsCaptureItem item{ nullptr };
    const HRESULT hr = interop->CreateForWindow(
        reinterpret_cast<HWND>(windowId), winrt::guid_of<GraphicsCaptureItem>(),
        winrt::put_abi(item));
    if (FAILED(hr))
        return nullptr;
    return item;
}

} // namespace

// ---------------------------------------------------------------- Impl

class LiveWindowSource::Impl
{
public:
    ~Impl() { shutdown(); }

    bool initialize(quintptr windowId, QString *error)
    {
        // WinRT 公寓必须先就绪：
        // 主线程是 Qt 的 STA，而 WGC 的 Direct3D11CaptureFramePool 内部
        // 要走 WinRT 激活，公寓没初始化时 CreateDirect3D11SurfaceFromDXGISurface
        // 会直接访问空指针（0xC0000005）。这里显式按 MTA 初始化一次。
        try {
            winrt::init_apartment(winrt::apartment_type::multi_threaded);
        } catch (const winrt::hresult_error &) {
            // RPC_E_CHANGED_MODE：当前线程已经以别的公寓模型初始化过，
            // 这不是致命错误，继续往下走。
        }

        m_device = createD3DDevice();
        if (!m_device) {
            setError(error, QStringLiteral("无法创建 D3D11 设备"));
            return false;
        }

        m_rtDevice = wrapD3DDevice(m_device);
        if (!m_rtDevice) {
            setError(error, QStringLiteral("无法包装 D3D11 设备"));
            return false;
        }

        m_item = createCaptureItem(windowId);
        if (!m_item) {
            setError(error, QStringLiteral("无法捕获该窗口（可能已被最小化或已关闭）"));
            return false;
        }

        m_size = m_item.Size();
        if (m_size.Width <= 0 || m_size.Height <= 0) {
            setError(error, QStringLiteral("窗口尺寸无效"));
            return false;
        }

        m_pool = Direct3D11CaptureFramePool::CreateFreeThreaded(
            m_rtDevice, DirectXPixelFormat::B8G8R8A8UIntNormalized, kPoolBuffers, m_size);
        if (!m_pool) {
            setError(error, QStringLiteral("无法创建帧池"));
            return false;
        }

        m_session = m_pool.CreateCaptureSession(m_item);
        if (!m_session) {
            setError(error, QStringLiteral("无法创建捕获会话"));
            return false;
        }

        // 不捕获光标：贴图是"窗口画面"，把鼠标箭头画进去会一直停在
        // 最后那个位置，看起来像卡住了。Win11 22H2+ 还能关掉黄色捕获边框。
        try {
            m_session.IsCursorCaptureEnabled(false);
        } catch (const hresult_error &) {
        }
        try {
            // IsBorderRequired 在 IGraphicsCaptureSession3（Win11 22H2+）
            if (auto s3 = m_session.try_as<IGraphicsCaptureSession3>())
                s3.IsBorderRequired(false);
        } catch (const hresult_error &) {
            // 旧系统没有该接口，捕获边框照常显示，不影响功能
        }
        try {
            // 脏区模式（snow 同款）：ReportOnly 让系统只更新变化区域，
            // 合成开销明显降低（4K 窗口尤其明显）。
            // 注意：这个属性在 IGraphicsCaptureSession4（Win11 22H2+），
            // 不是 Session2/3 —— 之前写错接口，编译器直接报错。
            if (auto s4 = m_session.try_as<IGraphicsCaptureSession4>())
                s4.DirtyRegionMode(GraphicsCaptureDirtyRegionMode::ReportOnly);
        } catch (const hresult_error &) {
            // 不支持就算了，不影响正确性
        }
        try {
            // 最小更新间隔（IGraphicsCaptureSession5）：
            // 让 WGC 自己按 30fps 出帧，省掉我们在 JS/UI 侧的节流判断。
            if (auto s5 = m_session.try_as<IGraphicsCaptureSession5>())
                s5.MinUpdateInterval(std::chrono::milliseconds(kFrameIntervalMs));
        } catch (const hresult_error &) {
        }

        m_item.Closed([this](const GraphicsCaptureItem &, const winrt::Windows::Foundation::IInspectable &) {
            m_closed = true;
        });

        // 帧回调在 D3D 线程池上，转回 Qt 主线程处理。
        // m_running 是 atomic：shutdown() 先把 m_running 置 false 再拆资源，
        // 回调一进来就能立刻退出，不会去碰已经析构的 pool/device。
        m_pool.FrameArrived([this](const Direct3D11CaptureFramePool &pool,
                                   const winrt::Windows::Foundation::IInspectable &) {
            if (!m_running.load(std::memory_order_acquire))
                return;
            try {
                onFrameArrived(pool);
            } catch (...) {
                // 捕获线程上任何异常都不能往外抛（会 terminate 整个进程）
            }
        });

        // 标记必须在 StartCapture 之前置位：首帧可能立刻到达
        m_running.store(true, std::memory_order_release);
        m_session.StartCapture();
        return true;
    }

    void shutdown()
    {
        // 先让回调停止工作
        m_running.store(false, std::memory_order_release);

        // 等正在处理的那一帧退出（D3D 回调可能正拿着 staging texture）
        {
            std::unique_lock<std::timed_mutex> lock(m_frameMutex, std::try_to_lock);
            // 拿不到锁说明回调还在跑，最多等 200ms；超时也照常拆资源
            //（m_running 已经置 false，回调不会再碰 D3D 对象）
            if (!lock.owns_lock()) {
                const bool acquired = lock.try_lock_for(std::chrono::milliseconds(200));
                Q_UNUSED(acquired);
            }
        }

        if (m_session) {
            try { m_session.Close(); } catch (...) {}
            m_session = nullptr;
        }
        if (m_pool) {
            try { m_pool.Close(); } catch (...) {}
            m_pool = nullptr;
        }
        m_item = nullptr;
        m_rtDevice = nullptr;
        m_device = nullptr;
        m_staging.Reset();
    }

    void onFrameArrived(const Direct3D11CaptureFramePool &pool)
    {
        // 与 shutdown() 互斥：读完一帧再放锁，避免拆资源时正在 memcpy
        std::lock_guard<std::timed_mutex> lock(m_frameMutex);

        // 关键优化（snow 的 drain_frame_pool 同款）：
        // **一次回调要把帧池里积压的帧全部取走**。
        // WGC 在内容不变时会合并/延迟回调，若每次只取一帧，
        // 池子里会一直残留旧帧，画面滞后且 Resize 后容易错乱。
        QImage image;
        int drained = 0;

        for (;;) {
            Direct3D11CaptureFrame frame{ nullptr };
            try {
                frame = pool.TryGetNextFrame();
            } catch (const winrt::hresult_error &) {
                break;   // 池空（E_ACCESSDENIED 之类）直接结束
            }
            if (!frame)
                break;

            const auto contentSize = frame.ContentSize();
            if (contentSize.Width != m_size.Width || contentSize.Height != m_size.Height) {
                m_size = contentSize;
                try {
                    pool.Recreate(m_rtDevice, DirectXPixelFormat::B8G8R8A8UIntNormalized,
                                  kPoolBuffers, m_size);
                } catch (const winrt::hresult_error &) {
                }
            }

            // 先把这一帧读成 QImage 再 Close：Close 之后 surface 可能失效。
            // 后面还有帧的话，后读到的会覆盖前面的 —— 最终留下的是最新画面。
            image = readbackToImage(frame.Surface());
            frame.Close();

            if (++drained >= kMaxDrainPerCallback)
                break;
        }

        if (image.isNull())
            return;

        // 节流：不是每帧都要交给 UI（30fps 足够）
        const qint64 now = QDateTime::currentMSecsSinceEpoch();
        if (m_lastDeliverMs != 0 && now - m_lastDeliverMs < kFrameIntervalMs)
            return;
        m_lastDeliverMs = now;

        if (deliverFrame)
            deliverFrame(std::move(image));
    }

    // GPU 纹理 → CPU 可读的 staging texture → QImage
    QImage readbackToImage(const IDirect3DSurface &surface)
    {
        if (!surface || !m_device)
            return {};

        // 整段包在异常保护里：WinRT 的 as<>/GetInterface 在类型不匹配或
        // 对象已失效时会抛 hresult_error，未捕获就会直接 terminate。
        // 崩溃堆栈就停在 CreateDirect3D11SurfaceFromDXGISurface 这一层。
        try {
            return readbackToImageImpl(surface);
        } catch (const winrt::hresult_error &e) {
            m_lastError = QString::fromWCharArray(e.message().c_str());
            return {};
        } catch (...) {
            return {};
        }
    }

    QImage readbackToImageImpl(const IDirect3DSurface &surface)
    {
        ComPtr<ID3D11Texture2D> texture;
        // IDirect3DDxgiInterfaceAccess 是 ABI 层接口，在 interop 头里
        const auto access =
            surface.as<::Windows::Graphics::DirectX::Direct3D11::IDirect3DDxgiInterfaceAccess>();
        if (!access)
            return {};
        if (FAILED(access->GetInterface(IID_PPV_ARGS(texture.GetAddressOf()))) || !texture)
            return {};

        D3D11_TEXTURE2D_DESC desc{};
        texture->GetDesc(&desc);
        if (desc.Width == 0 || desc.Height == 0)
            return {};

        // staging texture 每个尺寸只建一次
        if (!m_staging || m_stagingWidth != int(desc.Width) || m_stagingHeight != int(desc.Height)) {
            m_staging.Reset();
            D3D11_TEXTURE2D_DESC stagingDesc = desc;
            stagingDesc.Usage = D3D11_USAGE_STAGING;
            stagingDesc.BindFlags = 0;
            stagingDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
            stagingDesc.MiscFlags = 0;
            stagingDesc.MipLevels = 1;
            stagingDesc.ArraySize = 1;
            if (FAILED(m_device->CreateTexture2D(&stagingDesc, nullptr, m_staging.GetAddressOf())))
                return {};
            m_stagingWidth = int(desc.Width);
            m_stagingHeight = int(desc.Height);
        }

        ComPtr<ID3D11DeviceContext> context;
        m_device->GetImmediateContext(context.GetAddressOf());
        if (!context)
            return {};

        context->CopyResource(m_staging.Get(), texture.Get());

        D3D11_MAPPED_SUBRESOURCE mapped{};
        if (FAILED(context->Map(m_staging.Get(), 0, D3D11_MAP_READ, 0, &mapped)))
            return {};

        QImage image(int(desc.Width), int(desc.Height), QImage::Format_ARGB32_Premultiplied);
        if (!image.isNull()) {
            const auto *src = static_cast<const uchar *>(mapped.pData);
            for (int y = 0; y < image.height(); ++y) {
                std::memcpy(image.scanLine(y), src + qsizetype(y) * mapped.RowPitch,
                            size_t(image.width()) * 4);
            }
        }
        context->Unmap(m_staging.Get(), 0);
        return image;
    }

    void setError(QString *error, const QString &text)
    {
        if (error)
            *error = text;
        m_errorText = text;
    }

    // 由外层设置：把帧投递回 Qt 主线程
    std::function<void(QImage)> deliverFrame;

    ComPtr<ID3D11Device> m_device;
    winrt::Windows::Graphics::DirectX::Direct3D11::IDirect3DDevice m_rtDevice{ nullptr };
    GraphicsCaptureItem m_item{ nullptr };
    Direct3D11CaptureFramePool m_pool{ nullptr };
    GraphicsCaptureSession m_session{ nullptr };
    winrt::Windows::Graphics::SizeInt32 m_size{};

    ComPtr<ID3D11Texture2D> m_staging;
    int m_stagingWidth = 0;
    int m_stagingHeight = 0;

    qint64 m_lastDeliverMs = 0;
    bool m_closed = false;
    QString m_errorText;
    QString m_lastError;
    // 捕获是否仍在工作。shutdown() 先清它再拆 D3D 资源，
    // 帧回调据此提前退出，避免访问已释放对象。
    std::atomic<bool> m_running{ false };
    // 帧回调（D3D 线程）与 shutdown（主线程）互斥
    std::timed_mutex m_frameMutex;
};

// ---------------------------------------------------------------- 外层

LiveWindowSource::LiveWindowSource(QObject *parent)
    : QObject(parent)
    , m_impl(std::make_unique<Impl>())
{
    m_impl->deliverFrame = [this](QImage image) {
        // 从 D3D 回调线程转回 Qt 主线程
        QMetaObject::invokeMethod(this, [this, image = std::move(image)]() mutable {
            if (m_state != State::Live) {
                m_state = State::Live;
                m_stateMessage.clear();
                emit stateChanged(m_state, m_stateMessage);
            }
            m_lastFrame = image;
            emit frameReady(m_lastFrame);
        }, Qt::QueuedConnection);
    };
}

LiveWindowSource::~LiveWindowSource()
{
    stop();
}

bool LiveWindowSource::start(quintptr windowId, QString *error)
{
    stop();

    if (!windowId) {
        if (error)
            *error = QStringLiteral("窗口句柄无效");
        return false;
    }

    m_state = State::Initializing;
    m_stateMessage.clear();

    if (!m_impl->initialize(windowId, error)) {
        m_state = State::Unavailable;
        m_stateMessage = error ? *error : QString();
        emit stateChanged(m_state, m_stateMessage);
        return false;
    }

    RECT wr{};
    if (::GetWindowRect(reinterpret_cast<HWND>(windowId), &wr))
        m_windowRect = QRect(wr.left, wr.top, wr.right - wr.left, wr.bottom - wr.top);

    return true;
}

void LiveWindowSource::stop()
{
    if (m_impl)
        m_impl->shutdown();
    if (m_state != State::Closed) {
        m_state = State::Closed;
        emit stateChanged(m_state, m_stateMessage);
    }
}

bool LiveWindowSource::isRunning() const
{
    return m_impl && m_impl->m_session && m_state == State::Live;
}
