using Avalonia;
using Avalonia.Controls;
using Avalonia.Controls.Primitives;
using Avalonia.Input;
using Avalonia.Media;

namespace OneShot.Windows;

public partial class ColorPickerWindow : Window
{
    private static readonly Color[] PaletteColors =
    [
        Color.FromRgb(0x00, 0x00, 0x00),
        Color.FromRgb(0x43, 0x43, 0x43),
        Color.FromRgb(0x66, 0x66, 0x66),
        Color.FromRgb(0x99, 0x99, 0x99),
        Color.FromRgb(0xCC, 0xCC, 0xCC),
        Color.FromRgb(0xEF, 0xEF, 0xEF),
        Color.FromRgb(0xF3, 0xF3, 0xF3),
        Color.FromRgb(0xFF, 0xFF, 0xFF),
        Color.FromRgb(0x98, 0x00, 0x00),
        Color.FromRgb(0xFF, 0x00, 0x00),
        Color.FromRgb(0xFF, 0x99, 0x00),
        Color.FromRgb(0xFF, 0xFF, 0x00),
        Color.FromRgb(0x00, 0xFF, 0x00),
        Color.FromRgb(0x00, 0xFF, 0xFF),
        Color.FromRgb(0x4A, 0x86, 0xE8),
        Color.FromRgb(0x00, 0x00, 0xFF),
        Color.FromRgb(0x99, 0x00, 0xFF),
        Color.FromRgb(0xFF, 0x00, 0xFF),
        Color.FromRgb(0xE6, 0xB8, 0xAF),
        Color.FromRgb(0xF4, 0xCC, 0xCC),
        Color.FromRgb(0xF9, 0xCB, 0x9C),
        Color.FromRgb(0xFF, 0xE5, 0x99),
        Color.FromRgb(0xB6, 0xD7, 0xA8),
        Color.FromRgb(0xA2, 0xC4, 0xC9),
        Color.FromRgb(0xA4, 0xC2, 0xF4),
        Color.FromRgb(0x9F, 0xC5, 0xE8),
        Color.FromRgb(0xB4, 0xA7, 0xD6),
        Color.FromRgb(0xD5, 0xA6, 0xBD)
    ];

    private bool _isUpdating;

    public Color SelectedColor { get; private set; }

    public ColorPickerWindow(Color initialColor, string title = "Color Picker")
    {
        InitializeComponent();
        Title = title;
        SelectedColor = initialColor;
        BuildPalette();
        SetControlsFromColor(initialColor);

        AlphaSlider.PropertyChanged += (_, args) =>
        {
            if (args.Property == RangeBase.ValueProperty)
            {
                OnAlphaSliderChanged();
            }
        };

        HexText.LostFocus += (_, _) => TryApplyHex();
        HexText.KeyDown += OnHexTextKeyDown;
        OkButton.Click += (_, _) => Close(true);
        CancelButton.Click += (_, _) => Close(false);
    }

    private void OnAlphaSliderChanged()
    {
        if (_isUpdating)
        {
            return;
        }

        SelectedColor = Color.FromArgb((byte)AlphaSlider.Value, SelectedColor.R, SelectedColor.G, SelectedColor.B);
        UpdatePreviewAndHex(SelectedColor);
    }

    private void BuildPalette()
    {
        foreach (var paletteColor in PaletteColors)
        {
            var button = new Button
            {
                Width = 24,
                Height = 24,
                Margin = new Thickness(2),
                Padding = new Thickness(0),
                BorderThickness = new Thickness(1),
                BorderBrush = Brushes.Transparent,
                Background = new SolidColorBrush(paletteColor),
                Tag = paletteColor
            };
            ToolTip.SetTip(button, ToArgbHex(Color.FromArgb(255, paletteColor.R, paletteColor.G, paletteColor.B)));

            button.Click += OnPaletteButtonClick;
            PalettePanel.Children.Add(button);
        }
    }

    private void OnPaletteButtonClick(object? sender, Avalonia.Interactivity.RoutedEventArgs e)
    {
        if (sender is not Button { Tag: Color paletteColor })
        {
            return;
        }

        SelectedColor = Color.FromArgb((byte)AlphaSlider.Value, paletteColor.R, paletteColor.G, paletteColor.B);
        SetControlsFromColor(SelectedColor);
    }

    private void OnHexTextKeyDown(object? sender, KeyEventArgs e)
    {
        if (e.Key != Key.Enter)
        {
            return;
        }

        TryApplyHex();
        e.Handled = true;
    }

    private void TryApplyHex()
    {
        if (!TryParseColor(HexText.Text, out var color))
        {
            HexText.Text = ToArgbHex(SelectedColor);
            return;
        }

        SelectedColor = color;
        SetControlsFromColor(color);
    }

    private void SetControlsFromColor(Color color)
    {
        _isUpdating = true;
        AlphaSlider.Value = color.A;
        _isUpdating = false;
        UpdatePreviewAndHex(color);
        UpdatePaletteSelection(color);
    }

    private void UpdatePreviewAndHex(Color color)
    {
        PreviewBorder.Background = new SolidColorBrush(color);
        HexText.Text = ToArgbHex(color);
    }

    private void UpdatePaletteSelection(Color color)
    {
        foreach (var child in PalettePanel.Children)
        {
            if (child is not Button { Tag: Color paletteColor } button)
            {
                continue;
            }

            bool isMatch = paletteColor.R == color.R && paletteColor.G == color.G && paletteColor.B == color.B;
            button.BorderBrush = isMatch ? Brushes.Black : Brushes.Transparent;
            button.BorderThickness = isMatch ? new Thickness(2) : new Thickness(1);
        }
    }

    private static string ToArgbHex(Color color)
    {
        return $"#{color.A:X2}{color.R:X2}{color.G:X2}{color.B:X2}";
    }

    private static bool TryParseColor(string? input, out Color color)
    {
        color = Colors.Transparent;
        if (string.IsNullOrWhiteSpace(input))
        {
            return false;
        }

        string value = input.Trim();
        if (!value.StartsWith('#'))
        {
            value = $"#{value}";
        }

        if (!Color.TryParse(value, out var parsed))
        {
            return false;
        }

        if (value.Length == 7)
        {
            parsed = Color.FromArgb(255, parsed.R, parsed.G, parsed.B);
        }

        color = parsed;
        return true;
    }
}
