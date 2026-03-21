defmodule OneshotUi.Output do
  @moduledoc false

  alias OneshotCore.Capture.Image

  def save_bitmap(bitmap, path \\ nil) do
    config = OneshotCore.Config.fetch()
    File.mkdir_p!(config.save_dir)
    target = path || Path.join(config.save_dir, file_name())
    image = :wxBitmap.convertToImage(bitmap)

    if :wxImage.saveFile(image, target, ~c"image/png") do
      :wxImage.destroy(image)
      {:ok, target}
    else
      :wxImage.destroy(image)
      {:error, :save_failed}
    end
  end

  def copy_bitmap(bitmap) do
    clipboard = :wxClipboard.get()

    if :wxClipboard.open(clipboard) do
      data = :wxBitmapDataObject.new(bitmap)
      result = :wxClipboard.setData(clipboard, data)
      :wxClipboard.flush(clipboard)
      :wxClipboard.close(clipboard)
      if result, do: :ok, else: {:error, :clipboard_failed}
    else
      {:error, :clipboard_unavailable}
    end
  end

  def write_temp_drag_file(bitmap, path \\ nil) do
    target = path || OneshotCore.TempFileManager.allocate_drag_file_path()

    with {:ok, ^target} <- save_bitmap(bitmap, target),
         :ok <- OneshotCore.TempFileManager.register(target) do
      {:ok, target}
    end
  end

  def release_temp_drag_file(nil), do: :ok

  def release_temp_drag_file(path) do
    OneshotCore.TempFileManager.release(path)
  end

  def to_capture_image(bitmap) do
    image = :wxBitmap.convertToImage(bitmap)
    width = :wxImage.getWidth(image)
    height = :wxImage.getHeight(image)
    temp_path = OneshotCore.TempFileManager.allocate_drag_file_path()

    png =
      case :wxImage.saveFile(image, temp_path, ~c"image/png") do
        true ->
          data = File.read!(temp_path)
          _ = File.rm(temp_path)
          data

        false ->
          _ = File.rm(temp_path)
          <<>>
      end

    :wxImage.destroy(image)

    %Image{
      png: png,
      width: width,
      height: height,
      captured_at: DateTime.utc_now()
    }
  end

  def thumbnail(bitmap, max_size \\ 220) do
    image = :wxBitmap.convertToImage(bitmap)
    width = :wxImage.getWidth(image)
    height = :wxImage.getHeight(image)
    scale = min(max_size / max(width, 1), max_size / max(height, 1))
    scaled = :wxImage.scale(image, max(round(width * scale), 1), max(round(height * scale), 1))
    thumb = :wxBitmap.new(scaled)
    :wxImage.destroy(scaled)
    :wxImage.destroy(image)
    thumb
  end

  defp file_name do
    formatted =
      DateTime.utc_now()
      |> DateTime.shift_zone!("America/New_York")
      |> Calendar.strftime("%Y%m%d_%H%M%S_%f")

    "OneShot_#{formatted}.png"
  end
end
