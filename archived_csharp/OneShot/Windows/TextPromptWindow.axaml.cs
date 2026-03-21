using Avalonia.Controls;

namespace OneShot.Windows;

public partial class TextPromptWindow : Window
{
    public string? Value { get; private set; }

    public TextPromptWindow()
    {
        InitializeComponent();

        OkButton.Click += (_, _) =>
        {
            Value = InputText.Text;
            Close(true);
        };

        CancelButton.Click += (_, _) => Close(false);
        Opened += (_, _) => InputText.Focus();
    }
}