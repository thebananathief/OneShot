using Avalonia;
using Avalonia.Threading;
using OneShot.Models;
using OneShot.Windows;

namespace OneShot.Services;

public sealed class NotificationCoordinator
{
    private const double NotificationMargin = 16;
    private const double NotificationGap = 8;

    private readonly OutputService _outputService;
    private readonly List<NotificationWindow> _notifications = new();
    private readonly Dictionary<NotificationWindow, MarkupWindow> _markupWindows = new();

    public NotificationCoordinator(OutputService outputService)
    {
        _outputService = outputService;
    }

    public void ShowNotification(CapturedImage image)
    {
        _ = _outputService.CopyToClipboardAsync(image);

        var notification = new NotificationWindow(image, _outputService);
        notification.MarkupRequested += async (_, _) => await OpenMarkupAsync(notification);
        notification.DismissRequested += (_, _) => CloseNotification(notification, closeMarkupWindow: true);
        notification.Closed += (_, _) => OnNotificationClosed(notification);
        notification.Opened += (_, _) => Dispatcher.UIThread.Post(RepositionNotifications, DispatcherPriority.Background);
        notification.Show();

        _notifications.Insert(0, notification);
        Dispatcher.UIThread.Post(RepositionNotifications, DispatcherPriority.Background);
    }

    public void CloseAll()
    {
        foreach (var window in _markupWindows.Values.ToList())
        {
            window.Close();
        }

        foreach (var notification in _notifications.ToList())
        {
            notification.Close();
        }
    }

    private async Task OpenMarkupAsync(NotificationWindow notification)
    {
        if (_markupWindows.TryGetValue(notification, out var existingMarkupWindow))
        {
            existingMarkupWindow.Activate();
            return;
        }

        var markupWindow = new MarkupWindow(notification.CurrentImage, _outputService);
        _markupWindows[notification] = markupWindow;
        markupWindow.Closed += (_, _) =>
        {
            _markupWindows.Remove(notification);
            if (markupWindow.ResultImage is not null)
            {
                notification.UpdateImage(markupWindow.ResultImage);
            }
        };
        markupWindow.Show();

        await Task.CompletedTask;
    }

    private void OnNotificationClosed(NotificationWindow notification)
    {
        _notifications.Remove(notification);

        if (_markupWindows.Remove(notification, out var markupWindow) && markupWindow.IsVisible)
        {
            markupWindow.Close();
        }

        RepositionNotifications();
    }

    private void CloseNotification(NotificationWindow notification, bool closeMarkupWindow)
    {
        if (closeMarkupWindow && _markupWindows.Remove(notification, out var markupWindow) && markupWindow.IsVisible)
        {
            markupWindow.Close();
        }

        if (notification.IsVisible)
        {
            notification.Close();
        }
    }

    private void RepositionNotifications()
    {
        var firstVisible = _notifications.FirstOrDefault(n => n.IsVisible);
        var workingArea = firstVisible?.Screens?.Primary?.WorkingArea;
        if (workingArea is null)
        {
            var fallback = System.Windows.Forms.Screen.PrimaryScreen?.WorkingArea;
            if (fallback is null)
            {
                return;
            }

            workingArea = new PixelRect(fallback.Value.X, fallback.Value.Y, fallback.Value.Width, fallback.Value.Height);
        }

        double top = workingArea.Value.Y + NotificationMargin;

        foreach (var notification in _notifications.ToList())
        {
            if (!notification.IsVisible)
            {
                continue;
            }

            var placement = NotificationPlacement.Arrange(
                workingArea.Value,
                notification.Bounds,
                notification.RenderScaling,
                top,
                NotificationMargin,
                NotificationGap);

            notification.Position = placement.Position;
            top = placement.NextTopInPixels;
        }
    }
}
