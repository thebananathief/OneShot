defmodule OneshotUi.VirtualScreen do
  @moduledoc false

  def geometry do
    count = :wxDisplay.getCount()

    0..max(count - 1, 0)
    |> Enum.map(&display_geometry/1)
    |> Enum.reduce(nil, &union/2)
    |> case do
      nil -> %{left: 0, top: 0, width: 1, height: 1}
      geometry -> geometry
    end
  end

  defp display_geometry(index) do
    display = :wxDisplay.new(index)
    {x, y, width, height} = :wxDisplay.getGeometry(display)
    :wxDisplay.destroy(display)
    %{left: x, top: y, width: width, height: height}
  end

  defp union(geometry, nil), do: geometry

  defp union(a, b) do
    left = min(a.left, b.left)
    top = min(a.top, b.top)
    right = max(a.left + a.width, b.left + b.width)
    bottom = max(a.top + a.height, b.top + b.height)
    %{left: left, top: top, width: right - left, height: bottom - top}
  end
end
