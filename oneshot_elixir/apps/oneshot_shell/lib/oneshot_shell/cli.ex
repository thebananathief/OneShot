defmodule OneshotShell.CLI do
  @moduledoc false

  @default_timeout_ms 1_500

  require Logger

  def main(args) do
    Application.ensure_all_started(:logger)

    case normalize_args(args) do
      :daemon ->
        case OneshotCore.Ipc.Client.ping(timeout: @default_timeout_ms) do
          :ok -> :ok
          _ -> start_primary!()
        end

      {:send, command} ->
        run_single_command(command)

      :diagnostics ->
        print_diagnostics()
    end
  end

  defp normalize_args([]), do: :daemon
  defp normalize_args(["snapshot"]), do: {:send, "snapshot"}
  defp normalize_args(["install-startup"]), do: {:send, "install-startup"}
  defp normalize_args(["uninstall-startup"]), do: {:send, "uninstall-startup"}
  defp normalize_args(["diagnostics"]), do: :diagnostics
  defp normalize_args(_), do: {:send, "snapshot"}

  defp run_single_command(command) do
    case OneshotCore.Ipc.Client.send_command(command, timeout: @default_timeout_ms) do
      :ok ->
        :ok

      {:error, _reason} ->
        start_primary!()
        Process.sleep(150)
        OneshotCore.Ipc.Client.send_command(command, timeout: @default_timeout_ms)
    end
  end

  defp start_primary! do
    {:ok, _} = Application.ensure_all_started(:oneshot_core)

    case Application.ensure_all_started(:oneshot_ui) do
      {:ok, _} ->
        :ok

      {:error, reason} ->
        Logger.warning("OneshotUi failed to start: #{inspect(reason)}")
    end

    Process.sleep(:infinity)
  end

  defp print_diagnostics do
    config = OneshotCore.Config.fetch()

    diagnostics = %{
      release: to_string(Application.spec(:oneshot_core, :vsn) || "dev"),
      ipc_host: config.ipc_host,
      ipc_port: config.ipc_port,
      lock_path: config.lock_path,
      save_dir: config.save_dir,
      temp_dir: config.temp_dir,
      running: OneshotCore.Ipc.Client.ping(timeout: @default_timeout_ms) == :ok
    }

    IO.puts(inspect(diagnostics, pretty: true))
  end
end
