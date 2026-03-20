defmodule OneshotCore.Diagnostics do
  @moduledoc false

  def summary do
    config = OneshotCore.Config.fetch()

    [
      "IPC #{format_ip(config.ipc_host)}:#{config.ipc_port}",
      "Lock #{config.lock_path}",
      "Save #{config.save_dir}",
      "Temp #{config.temp_dir}"
    ]
    |> Enum.join("\n")
  end

  defp format_ip({a, b, c, d}), do: Enum.join([a, b, c, d], ".")
end
