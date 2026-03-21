defmodule OneshotUi.ScreenCapture do
  @moduledoc false

  def capture_virtual_screen do
    geometry = OneshotUi.VirtualScreen.geometry()
    bitmap = capture_bitmap(geometry)
    %{bitmap: bitmap, geometry: geometry}
  end

  def crop_bitmap(bitmap, geometry, selection) do
    left = max(0, selection.left - geometry.left)
    top = max(0, selection.top - geometry.top)
    width = min(selection.width, geometry.width - left)
    height = min(selection.height, geometry.height - top)

    :wxBitmap.getSubBitmap(bitmap, {left, top, width, height})
  end

  def bitmap_dimensions(bitmap) do
    {:wxBitmap.getWidth(bitmap), :wxBitmap.getHeight(bitmap)}
  end

  def destroy_bitmap(bitmap) when is_tuple(bitmap) do
    :wxBitmap.destroy(bitmap)
    :ok
  rescue
    _ -> :ok
  catch
    _, _ -> :ok
  end

  def destroy_bitmap(_), do: :ok

  defp capture_bitmap(%{left: left, top: top, width: width, height: height}) do
    screen_dc = :wxScreenDC.new()
    bitmap = :wxBitmap.new(width, height)
    memory_dc = :wxMemoryDC.new(bitmap)
    :wxMemoryDC.blit(memory_dc, {0, 0}, {width, height}, screen_dc, {left, top})
    :wxMemoryDC.destroy(memory_dc)
    :wxScreenDC.destroy(screen_dc)
    bitmap
  end
end
