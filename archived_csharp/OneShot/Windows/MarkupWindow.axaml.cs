using Avalonia;
using Avalonia.Controls;
using Avalonia.Controls.Primitives;
using Avalonia.Input;
using Avalonia.Interactivity;
using Avalonia.Media;
using Avalonia.VisualTree;
using Avalonia.Controls.Shapes;
using Avalonia.Media.Imaging;
using Avalonia.Threading;
using OneShot.Models;
using OneShot.Services;

namespace OneShot.Windows;

public partial class MarkupWindow : Window
{
    private enum ToolType
    {
        Pen,
        Line,
        Arrow,
        Rectangle,
        Ellipse,
        Text
    }

    private enum ColorTarget
    {
        Stroke,
        Fill,
        Font
    }

    private readonly OutputService _output;
    private readonly CapturedImage _sourceImage;
    private readonly IMarkupImageExportService _imageExportService;
    private readonly MarkupPrimitiveBuilder _primitiveBuilder;
    private readonly Stack<Control> _undo = new();
    private readonly Stack<Control> _redo = new();
    private readonly ScaleTransform _editorScaleTransform;
    private readonly double _sourceWidthDip;
    private readonly double _sourceHeightDip;

    private const double MaxZoom = 8.0;
    private const double FitPaddingFactor = 0.99;
    private const double ScrollbarAllowance = 18.0;
    private const double AngleSnapIncrementRadians = Math.PI / 12.0;
    private double _zoom = 1.0;
    private double _minimumZoom = 0.1;
    private bool _hasInitialFit;
    private bool _isApplyingInitialFit;
    private bool _userAdjustedViewport;
    private bool _isMiddlePanning;
    private Point _panStartMouse;
    private double _panStartHorizontalOffset;
    private double _panStartVerticalOffset;

    private Point? _start;
    private Shape? _shape;
    private Polyline? _polyline;

    private Color _strokeColor = Colors.Red;
    private Color _fillColor = Color.FromArgb(64, 255, 0, 0);
    private Color _fontColor = Colors.Red;

    public CapturedImage? ResultImage { get; private set; }

    public MarkupWindow(CapturedImage image, OutputService output, IMarkupImageExportService? imageExportService = null)
    {
        InitializeComponent();
        _output = output;
        _sourceImage = image;
        _imageExportService = imageExportService ?? new MarkupImageExportService();
        _primitiveBuilder = new MarkupPrimitiveBuilder();
        _editorScaleTransform = new ScaleTransform(1, 1);
        EditorTransformHost.LayoutTransform = _editorScaleTransform;
        _sourceWidthDip = Math.Max(1, _sourceImage.Bitmap.Size.Width);
        _sourceHeightDip = Math.Max(1, _sourceImage.Bitmap.Size.Height);

        BaseImage.Source = _sourceImage.Bitmap;
        BaseImage.Width = _sourceWidthDip;
        BaseImage.Height = _sourceHeightDip;
        OverlayCanvas.Width = _sourceWidthDip;
        OverlayCanvas.Height = _sourceHeightDip;
        EditorSurface.Width = _sourceWidthDip;
        EditorSurface.Height = _sourceHeightDip;

        ToolCombo.ItemsSource = Enum.GetValues<ToolType>();
        ToolCombo.SelectedItem = ToolType.Pen;
        ToolCombo.SelectionChanged += (_, _) => UpdateOptionsVisibility();

        FontCombo.ItemsSource = FontManager.Current.SystemFonts.Select(f => f.Name).Distinct().OrderBy(n => n).ToList();
        FontCombo.SelectedItem = "Segoe UI";

        OptionsButton.Click += (_, _) =>
        {
            UpdateOptionsVisibility();
            OptionsPopup.IsOpen = true;
        };

        LineColorButton.Click += async (_, _) => await PickColorAsync(ColorTarget.Stroke, "Select Line Color");
        ShapeStrokeColorButton.Click += async (_, _) => await PickColorAsync(ColorTarget.Stroke, "Select Stroke Color");
        ShapeFillColorButton.Click += async (_, _) => await PickColorAsync(ColorTarget.Fill, "Select Fill Color");
        TextColorButton.Click += async (_, _) => await PickColorAsync(ColorTarget.Font, "Select Font Color");

        ShapeFillOpacitySlider.PropertyChanged += (_, args) =>
        {
            if (args.Property == RangeBase.ValueProperty)
            {
                SyncFillOpacityFromSlider();
            }
        };

        OverlayCanvas.PointerPressed += OnCanvasPointerPressed;
        OverlayCanvas.PointerMoved += OnCanvasPointerMoved;
        OverlayCanvas.PointerReleased += OnCanvasPointerReleased;

        EditorScrollViewer.PointerWheelChanged += OnEditorPointerWheelChanged;
        EditorScrollViewer.AddHandler(PointerPressedEvent, OnEditorPointerPressed, RoutingStrategies.Tunnel | RoutingStrategies.Bubble, true);
        EditorScrollViewer.AddHandler(PointerMovedEvent, OnEditorPointerMoved, RoutingStrategies.Tunnel | RoutingStrategies.Bubble, true);
        EditorScrollViewer.AddHandler(PointerReleasedEvent, OnEditorPointerReleased, RoutingStrategies.Tunnel | RoutingStrategies.Bubble, true);

        EditorScrollViewer.PropertyChanged += (_, args) =>
        {
            if (args.Property == BoundsProperty)
            {
                UpdateZoomBounds(forceFitIfAtMinimum: true);
            }
        };
        EditorScrollViewer.LayoutUpdated += (_, _) => EnsureInitialFit();

        Opened += (_, _) =>
        {
            // Initial fit is deferred until a real viewport is available.
            Dispatcher.UIThread.Post(EnsureInitialFit, DispatcherPriority.Loaded);
            Dispatcher.UIThread.Post(EnsureInitialFit, DispatcherPriority.Background);
            _ = StabilizeInitialFitAsync();
        };
        KeyDown += OnWindowKeyDown;

        UndoButton.Click += (_, _) => Undo();
        RedoButton.Click += (_, _) => Redo();
        ClipboardButton.Click += async (_, _) => await _output.CopyToClipboardAsync(RenderCurrentImage());
        DoneButton.Click += async (_, _) =>
        {
            ResultImage = RenderCurrentImage();
            await _output.SaveAsync(ResultImage);
            Close();
        };

        ShapeFillOpacitySlider.Value = _fillColor.A / 255.0;
        UpdateColorSwatches();
        UpdateOptionsVisibility();
    }

    private void OnWindowKeyDown(object? sender, KeyEventArgs e)
    {
        if (!e.KeyModifiers.HasFlag(KeyModifiers.Control))
        {
            return;
        }

        if (e.Key == Key.Z)
        {
            Undo();
            e.Handled = true;
        }
        else if (e.Key == Key.Y)
        {
            Redo();
            e.Handled = true;
        }
    }

    private void UpdateOptionsVisibility()
    {
        var tool = (ToolType)(ToolCombo.SelectedItem ?? ToolType.Pen);

        bool isLineTool = tool is ToolType.Pen or ToolType.Line or ToolType.Arrow;
        bool isShapeTool = tool is ToolType.Rectangle or ToolType.Ellipse;
        bool isTextTool = tool == ToolType.Text;

        LineOptionsPanel.IsVisible = isLineTool;
        ShapeOptionsPanel.IsVisible = isShapeTool;
        TextOptionsPanel.IsVisible = isTextTool;

        OptionsTitle.Text = tool switch
        {
            ToolType.Rectangle => "Rectangle Options",
            ToolType.Ellipse => "Ellipse Options",
            ToolType.Text => "Text Options",
            _ => "Line Options"
        };
    }

    private async Task PickColorAsync(ColorTarget target, string title)
    {
        Color initial = target switch
        {
            ColorTarget.Fill => _fillColor,
            ColorTarget.Font => _fontColor,
            _ => _strokeColor
        };

        var picker = new ColorPickerWindow(initial, title);
        bool accepted = await picker.ShowDialog<bool>(this);
        if (!accepted)
        {
            return;
        }

        switch (target)
        {
            case ColorTarget.Fill:
                _fillColor = picker.SelectedColor;
                ShapeFillOpacitySlider.Value = _fillColor.A / 255.0;
                break;
            case ColorTarget.Font:
                _fontColor = picker.SelectedColor;
                break;
            default:
                _strokeColor = picker.SelectedColor;
                break;
        }

        UpdateColorSwatches();
    }

    private void SyncFillOpacityFromSlider()
    {
        byte alpha = (byte)Math.Round(ShapeFillOpacitySlider.Value * 255);
        _fillColor = Color.FromArgb(alpha, _fillColor.R, _fillColor.G, _fillColor.B);
        UpdateColorSwatches();
    }

    private void UpdateColorSwatches()
    {
        var strokeBrush = new SolidColorBrush(_strokeColor);
        var fillBrush = new SolidColorBrush(_fillColor);
        var fontBrush = new SolidColorBrush(_fontColor);

        LineColorSwatch.Background = strokeBrush;
        ShapeStrokeColorSwatch.Background = strokeBrush;
        ShapeFillColorSwatch.Background = fillBrush;
        TextColorSwatch.Background = fontBrush;

        string strokeHex = ToArgbHex(_strokeColor);
        string fillHex = ToArgbHex(_fillColor);
        string fontHex = ToArgbHex(_fontColor);

        LineColorText.Text = strokeHex;
        ShapeStrokeColorText.Text = strokeHex;
        ShapeFillColorText.Text = fillHex;
        TextColorText.Text = fontHex;
    }

    private async void OnCanvasPointerPressed(object? sender, PointerPressedEventArgs e)
    {
        if (!e.GetCurrentPoint(OverlayCanvas).Properties.IsLeftButtonPressed)
        {
            return;
        }

        var tool = (ToolType)(ToolCombo.SelectedItem ?? ToolType.Pen);
        var point = e.GetPosition(OverlayCanvas);

        if (tool == ToolType.Text)
        {
            var prompt = new TextPromptWindow();
            bool accepted = await prompt.ShowDialog<bool>(this);
            if (accepted && !string.IsNullOrWhiteSpace(prompt.Value))
            {
                var text = new TextBlock
                {
                    Text = prompt.Value,
                    Foreground = new SolidColorBrush(_fontColor),
                    FontSize = FontSizeSlider.Value,
                    FontFamily = new FontFamily((string?)(FontCombo.SelectedItem as string) ?? "Segoe UI")
                };

                Canvas.SetLeft(text, point.X);
                Canvas.SetTop(text, point.Y);
                AddElement(text);
            }

            return;
        }

        _start = point;
        e.Pointer.Capture(OverlayCanvas);

        switch (tool)
        {
            case ToolType.Pen:
                _polyline = new Polyline
                {
                    Stroke = new SolidColorBrush(_strokeColor),
                    StrokeThickness = LineSizeSlider.Value,
                    StrokeJoin = PenLineJoin.Round,
                    StrokeLineCap = PenLineCap.Round
                };
                _polyline.Points = [point];
                OverlayCanvas.Children.Add(_polyline);
                break;
            case ToolType.Line:
            case ToolType.Arrow:
                _shape = new Line
                {
                    Stroke = new SolidColorBrush(_strokeColor),
                    StrokeThickness = LineSizeSlider.Value,
                    StartPoint = point,
                    EndPoint = point
                };
                OverlayCanvas.Children.Add(_shape);
                break;
            case ToolType.Rectangle:
                _shape = new Rectangle
                {
                    Stroke = new SolidColorBrush(_strokeColor),
                    StrokeThickness = ShapeStrokeSizeSlider.Value,
                    Fill = BuildFillBrush()
                };
                OverlayCanvas.Children.Add(_shape);
                break;
            case ToolType.Ellipse:
                _shape = new Ellipse
                {
                    Stroke = new SolidColorBrush(_strokeColor),
                    StrokeThickness = ShapeStrokeSizeSlider.Value,
                    Fill = BuildFillBrush()
                };
                OverlayCanvas.Children.Add(_shape);
                break;
        }
    }

    private void OnCanvasPointerMoved(object? sender, PointerEventArgs e)
    {
        if (_start is null)
        {
            return;
        }

        if (!e.GetCurrentPoint(OverlayCanvas).Properties.IsLeftButtonPressed)
        {
            return;
        }

        var point = e.GetPosition(OverlayCanvas);
        bool snapToAxes = e.KeyModifiers.HasFlag(KeyModifiers.Shift);
        if (_polyline is not null)
        {
            _polyline.Points = _polyline.Points.Append(point).ToList();
            return;
        }

        if (_shape is Line line)
        {
            line.EndPoint = snapToAxes ? SnapLineEndpoint(_start.Value, point) : point;
            return;
        }

        if (_shape is Rectangle or Ellipse)
        {
            var rect = BuildRect(_start.Value, point, snapToAxes);
            Canvas.SetLeft(_shape, rect.X);
            Canvas.SetTop(_shape, rect.Y);
            _shape.Width = rect.Width;
            _shape.Height = rect.Height;
        }
    }

    private void OnCanvasPointerReleased(object? sender, PointerReleasedEventArgs e)
    {
        e.Pointer.Capture(null);

        var tool = (ToolType)(ToolCombo.SelectedItem ?? ToolType.Pen);
        if (_polyline is not null)
        {
            AddElement(_polyline);
            _polyline = null;
            _start = null;
            return;
        }

        if (_shape is not null)
        {
            if (_start is null)
            {
                _shape = null;
                return;
            }

            var start = _start.Value;
            var releasePoint = e.GetPosition(OverlayCanvas);
            bool snapToAxes = e.KeyModifiers.HasFlag(KeyModifiers.Shift);
            if (_shape is Line drawnLine)
            {
                drawnLine.EndPoint = snapToAxes ? SnapLineEndpoint(start, releasePoint) : releasePoint;
            }
            else if (_shape is Rectangle or Ellipse)
            {
                var rect = BuildRect(start, releasePoint, snapToAxes);
                Canvas.SetLeft(_shape, rect.X);
                Canvas.SetTop(_shape, rect.Y);
                _shape.Width = rect.Width;
                _shape.Height = rect.Height;
            }

            AddElement(_shape);
            if (tool == ToolType.Arrow && _shape is Line line)
            {
                AddArrowHead(line);
            }

            _shape = null;
            _start = null;
        }
    }

    private void OnEditorPointerWheelChanged(object? sender, PointerWheelEventArgs e)
    {
        if (!e.KeyModifiers.HasFlag(KeyModifiers.Control))
        {
            return;
        }

        _userAdjustedViewport = true;
        e.Handled = true;
        UpdateZoomBounds(forceFitIfAtMinimum: false);

        double step = e.Delta.Y > 0 ? 1.1 : (1.0 / 1.1);
        double targetZoom = Math.Clamp(_zoom * step, _minimumZoom, MaxZoom);
        if (Math.Abs(targetZoom - _zoom) < 0.0001)
        {
            return;
        }

        var mouse = e.GetPosition(EditorScrollViewer);
        var sourcePoint = ViewportToImagePoint(mouse);

        SetZoom(targetZoom);
        EditorScrollViewer.UpdateLayout();
        var hostOrigin = GetHostOrigin();
        EditorScrollViewer.Offset = ClampOffset(new Vector(
            sourcePoint.X * targetZoom + hostOrigin.X - mouse.X,
            sourcePoint.Y * targetZoom + hostOrigin.Y - mouse.Y));
    }

    private void OnEditorPointerPressed(object? sender, PointerPressedEventArgs e)
    {
        if (!e.GetCurrentPoint(EditorScrollViewer).Properties.IsMiddleButtonPressed)
        {
            return;
        }

        if (EditorScrollViewer.Extent.Width <= EditorScrollViewer.Viewport.Width && EditorScrollViewer.Extent.Height <= EditorScrollViewer.Viewport.Height)
        {
            return;
        }

        _isMiddlePanning = true;
        _userAdjustedViewport = true;
        _panStartMouse = e.GetPosition(this);
        _panStartHorizontalOffset = EditorScrollViewer.Offset.X;
        _panStartVerticalOffset = EditorScrollViewer.Offset.Y;
        e.Pointer.Capture(EditorScrollViewer);
        EditorScrollViewer.Cursor = new Cursor(StandardCursorType.SizeAll);
        e.Handled = true;
    }

    private void OnEditorPointerMoved(object? sender, PointerEventArgs e)
    {
        if (!_isMiddlePanning)
        {
            return;
        }

        if (!e.GetCurrentPoint(EditorScrollViewer).Properties.IsMiddleButtonPressed)
        {
            StopMiddlePan();
            return;
        }

        var point = e.GetPosition(this);
        var delta = point - _panStartMouse;

        EditorScrollViewer.Offset = ClampOffset(new Vector(
            Math.Clamp(_panStartHorizontalOffset - delta.X, 0, Math.Max(0, EditorScrollViewer.Extent.Width - EditorScrollViewer.Viewport.Width)),
            Math.Clamp(_panStartVerticalOffset - delta.Y, 0, Math.Max(0, EditorScrollViewer.Extent.Height - EditorScrollViewer.Viewport.Height))));
        e.Handled = true;
    }

    private void OnEditorPointerReleased(object? sender, PointerReleasedEventArgs e)
    {
        if (!_isMiddlePanning)
        {
            return;
        }

        StopMiddlePan();
        e.Handled = true;
    }

    private void StopMiddlePan()
    {
        _isMiddlePanning = false;
        EditorScrollViewer.Cursor = Cursor.Default;
    }

    private void FitImageToViewport()
    {
        if (!ApplyFitZoom())
        {
            return;
        }
        _hasInitialFit = true;
    }

    private bool UpdateZoomBounds(bool forceFitIfAtMinimum)
    {
        if (!TryComputeFitZoom(out double fit))
        {
            return false;
        }

        bool wasAtMinimum = Math.Abs(_zoom - _minimumZoom) < 0.0001;
        _minimumZoom = fit;
        UpdateZoomLayout();

        bool shouldInitialFit = !_hasInitialFit;
        if (shouldInitialFit || _zoom < _minimumZoom || (forceFitIfAtMinimum && wasAtMinimum))
        {
            if (!ApplyFitZoom())
            {
                return false;
            }
            _hasInitialFit = true;
        }

        return true;
    }

    private void SetZoom(double zoom)
    {
        _zoom = zoom;
        _editorScaleTransform.ScaleX = zoom;
        _editorScaleTransform.ScaleY = zoom;
        UpdateZoomLayout();
    }

    private void UpdateZoomLayout()
    {
        double contentWidth = _sourceWidthDip * _zoom;
        double contentHeight = _sourceHeightDip * _zoom;

        var viewport = GetViewportSizeForLayout();
        EditorContainer.Width = Math.Max(contentWidth, viewport.Width);
        EditorContainer.Height = Math.Max(contentHeight, viewport.Height);
    }

    private bool TryGetViewportSize(out Size viewport)
    {
        double width = EditorScrollViewer.Viewport.Width;
        double height = EditorScrollViewer.Viewport.Height;
        if (width > 0 && height > 0)
        {
            viewport = new Size(width, height);
            return true;
        }

        viewport = default;
        return false;
    }
    private bool TryGetRenderedContentSize(out Size rendered)
    {
        rendered = EditorTransformHost.Bounds.Size;
        return rendered.Width > 0 && rendered.Height > 0;
    }

    private Size GetViewportSizeForLayout()
    {
        if (TryGetViewportSize(out var viewport))
        {
            return viewport;
        }

        return EditorScrollViewer.Bounds.Size;
    }

    private Point GetHostOrigin()
    {
        double x = Math.Max(0, (EditorContainer.Bounds.Width - EditorTransformHost.Bounds.Width) / 2);
        double y = Math.Max(0, (EditorContainer.Bounds.Height - EditorTransformHost.Bounds.Height) / 2);
        return new Point(x, y);
    }

    private Point ViewportToImagePoint(Point viewportPoint)
    {
        var hostOrigin = GetHostOrigin();
        double x = (EditorScrollViewer.Offset.X + viewportPoint.X - hostOrigin.X) / _zoom;
        double y = (EditorScrollViewer.Offset.Y + viewportPoint.Y - hostOrigin.Y) / _zoom;
        return new Point(
            Math.Clamp(x, 0, _sourceWidthDip),
            Math.Clamp(y, 0, _sourceHeightDip));
    }

    private Vector ClampOffset(Vector offset)
    {
        double maxX = Math.Max(0, EditorScrollViewer.Extent.Width - EditorScrollViewer.Viewport.Width);
        double maxY = Math.Max(0, EditorScrollViewer.Extent.Height - EditorScrollViewer.Viewport.Height);
        return new Vector(
            Math.Clamp(offset.X, 0, maxX),
            Math.Clamp(offset.Y, 0, maxY));
    }

    private void EnsureInitialFit()
    {
        if (_hasInitialFit || _isApplyingInitialFit)
        {
            return;
        }

        if (!TryGetViewportSize(out _))
        {
            Dispatcher.UIThread.Post(EnsureInitialFit, DispatcherPriority.Background);
            return;
        }

        _isApplyingInitialFit = true;
        try
        {
            FitImageToViewport();
        }
        finally
        {
            _isApplyingInitialFit = false;
        }
    }

    private async Task StabilizeInitialFitAsync()
    {
        // Re-apply fit across a few render passes to avoid late layout/scroll extent races.
        for (int i = 0; i < 8; i++)
        {
            if (_userAdjustedViewport)
            {
                return;
            }

            await Dispatcher.UIThread.InvokeAsync(() =>
            {
                if (!_userAdjustedViewport)
                {
                    FitImageToViewport();
                }
            }, DispatcherPriority.Render);
        }
    }

    private bool TryComputeFitZoom(out double fit)
    {
        fit = 1.0;
        if (!TryGetViewportSize(out var viewport))
        {
            return false;
        }

        double sourceWidth = EditorSurface.Width;
        double sourceHeight = EditorSurface.Height;
        double availableWidth = Math.Max(1, viewport.Width - ScrollbarAllowance);
        double availableHeight = Math.Max(1, viewport.Height - ScrollbarAllowance);
        if (availableWidth <= 0 || availableHeight <= 0 || sourceWidth <= 0 || sourceHeight <= 0)
        {
            return false;
        }

        fit = Math.Min(availableWidth / sourceWidth, availableHeight / sourceHeight) * FitPaddingFactor;
        return !double.IsNaN(fit) && !double.IsInfinity(fit) && fit > 0;
    }

    private bool ApplyFitZoom()
    {
        if (!TryComputeFitZoom(out double firstFit))
        {
            return false;
        }

        _minimumZoom = firstFit;
        SetZoom(firstFit);
        EditorScrollViewer.UpdateLayout();

        if (!TryComputeFitZoom(out double settledFit))
        {
            return false;
        }

        _minimumZoom = settledFit;
        if (Math.Abs(settledFit - _zoom) > 0.0001)
        {
            SetZoom(settledFit);
            EditorScrollViewer.UpdateLayout();
        }

        // Final correction based on actual rendered size, not layout assumptions.
        if (TryGetViewportSize(out var viewport))
        {
            for (int i = 0; i < 24; i++)
            {
                if (!TryGetRenderedContentSize(out var rendered))
                {
                    break;
                }

                double fitWidth = Math.Max(1, viewport.Width - 2);
                double fitHeight = Math.Max(1, viewport.Height - 2);
                double overshoot = Math.Max(rendered.Width / fitWidth, rendered.Height / fitHeight);
                if (double.IsNaN(overshoot) || double.IsInfinity(overshoot) || overshoot <= 0)
                {
                    break;
                }

                if (overshoot <= 1.0)
                {
                    break;
                }

                double correctedZoom = _zoom / overshoot * 0.995;
                SetZoom(correctedZoom);
                EditorScrollViewer.UpdateLayout();
            }

            _minimumZoom = _zoom;
        }

        // Hard guarantee: initial fit should not require scrolling on either axis.
        if (TryGetViewportSize(out _))
        {
            for (int i = 0; i < 24; i++)
            {
                double extentW = EditorScrollViewer.Extent.Width;
                double extentH = EditorScrollViewer.Extent.Height;
                double viewW = EditorScrollViewer.Viewport.Width;
                double viewH = EditorScrollViewer.Viewport.Height;
                if (viewW <= 0 || viewH <= 0 || extentW <= 0 || extentH <= 0)
                {
                    break;
                }

                double overW = extentW / viewW;
                double overH = extentH / viewH;
                double overshoot = Math.Max(overW, overH);
                if (overshoot <= 1.0)
                {
                    break;
                }

                double correctedZoom = _zoom / overshoot * 0.995;
                SetZoom(correctedZoom);
                EditorScrollViewer.UpdateLayout();
            }

            _minimumZoom = _zoom;
        }

        EditorScrollViewer.Offset = default;
        return true;
    }

    private void AddArrowHead(Line line)
    {
        var start = line.StartPoint;
        var end = line.EndPoint;
        var angle = Math.Atan2(end.Y - start.Y, end.X - start.X);
        var headLength = line.StrokeThickness * 4;
        const double wingAngle = Math.PI / 7;

        Point p1 = new(end.X - headLength * Math.Cos(angle - wingAngle), end.Y - headLength * Math.Sin(angle - wingAngle));
        Point p2 = new(end.X - headLength * Math.Cos(angle + wingAngle), end.Y - headLength * Math.Sin(angle + wingAngle));

        var head = new Polygon
        {
            Fill = line.Stroke,
            Stroke = line.Stroke,
            StrokeThickness = line.StrokeThickness,
            Points = [end, p1, p2]
        };
        AddElement(head);
    }

    private void AddElement(Control element)
    {
        if (!OverlayCanvas.Children.Contains(element))
        {
            OverlayCanvas.Children.Add(element);
        }

        _undo.Push(element);
        _redo.Clear();
    }

    private void Undo()
    {
        if (_undo.Count == 0)
        {
            return;
        }

        var element = _undo.Pop();
        OverlayCanvas.Children.Remove(element);
        _redo.Push(element);
    }

    private void Redo()
    {
        if (_redo.Count == 0)
        {
            return;
        }

        var element = _redo.Pop();
        OverlayCanvas.Children.Add(element);
        _undo.Push(element);
    }

    private CapturedImage RenderCurrentImage()
    {
        var primitives = _primitiveBuilder.Build(
            OverlayCanvas.Children.OfType<Control>(),
            _sourceImage.PixelWidth,
            _sourceImage.PixelHeight,
            _sourceWidthDip,
            _sourceHeightDip);
        return _imageExportService.Export(_sourceImage, primitives);
    }

    private IBrush BuildFillBrush()
    {
        return new SolidColorBrush(_fillColor);
    }

    private static string ToArgbHex(Color color)
    {
        return $"#{color.A:X2}{color.R:X2}{color.G:X2}{color.B:X2}";
    }

    private static Rect BuildRect(Point a, Point b, bool keepAspectRatio)
    {
        var delta = b - a;
        if (!keepAspectRatio)
        {
            return new Rect(Math.Min(a.X, b.X), Math.Min(a.Y, b.Y), Math.Abs(delta.X), Math.Abs(delta.Y));
        }

        double side = Math.Max(Math.Abs(delta.X), Math.Abs(delta.Y));
        double x = delta.X < 0 ? a.X - side : a.X;
        double y = delta.Y < 0 ? a.Y - side : a.Y;
        return new Rect(x, y, side, side);
    }

    private static Point SnapLineEndpoint(Point start, Point rawEnd)
    {
        var delta = rawEnd - start;
        double distance = Math.Sqrt((delta.X * delta.X) + (delta.Y * delta.Y));
        if (distance < double.Epsilon)
        {
            return rawEnd;
        }

        double angle = Math.Atan2(delta.Y, delta.X);
        double snappedAngle = Math.Round(angle / AngleSnapIncrementRadians) * AngleSnapIncrementRadians;
        return new Point(
            start.X + (distance * Math.Cos(snappedAngle)),
            start.Y + (distance * Math.Sin(snappedAngle)));
    }
}

