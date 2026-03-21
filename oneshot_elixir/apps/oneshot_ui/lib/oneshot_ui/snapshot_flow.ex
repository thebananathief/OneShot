defmodule OneshotUi.SnapshotFlow do
  @moduledoc false

  require Logger

  def run do
    captured = OneshotUi.ScreenCapture.capture_virtual_screen()

    case OneshotUi.SelectionOverlay.select(captured.bitmap, captured.geometry) do
      {:ok, selection} ->
        cropped = OneshotUi.ScreenCapture.crop_bitmap(captured.bitmap, captured.geometry, selection)
        OneshotUi.ScreenCapture.destroy_bitmap(captured.bitmap)
        result = finalize_capture(cropped)
        result

      :cancel ->
        OneshotUi.ScreenCapture.destroy_bitmap(captured.bitmap)
        :cancel
    end
  rescue
    error ->
      Logger.error("Snapshot flow failed: #{Exception.message(error)}")
      {:error, error}
  end

  defp finalize_capture(bitmap) do
    with :ok <- OneshotUi.Output.copy_bitmap(bitmap),
         {:ok, path} <- OneshotUi.Output.save_bitmap(bitmap),
         {:ok, temp_file} <- OneshotUi.Output.write_temp_drag_file(bitmap),
         {:ok, _pid} <- OneshotUi.NotificationManager.show(bitmap, path, temp_file) do
      {:ok, path}
    else
      {:error, reason} ->
        OneshotUi.ScreenCapture.destroy_bitmap(bitmap)
        {:error, reason}
    end
  end
end
