defmodule OneshotCore.StartupRegistration do
  @moduledoc false

  alias OneshotCore.Config

  def install(config \\ Config.fetch()) do
    exe_path = System.find_executable("oneshot") || System.find_executable("oneshot.bat") || ""

    if exe_path == "" do
      {:error, :executable_not_found}
    else
      run_reg([
        "add",
        config.startup_key,
        "/v",
        config.app_name,
        "/t",
        "REG_SZ",
        "/d",
        "\"#{exe_path}\"",
        "/f"
      ])
    end
  end

  def uninstall(config \\ Config.fetch()) do
    run_reg(["delete", config.startup_key, "/v", config.app_name, "/f"])
  end

  defp run_reg(args) do
    case System.cmd("reg.exe", args, stderr_to_stdout: true) do
      {_output, 0} -> :ok
      {output, status} -> {:error, {:reg_failed, status, output}}
    end
  end
end
