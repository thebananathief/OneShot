defmodule OneshotCore.InstanceLockTest do
  use ExUnit.Case, async: false

  alias OneshotCore.Config
  alias OneshotCore.InstanceLock

  setup do
    base = Path.join(System.tmp_dir!(), "oneshot-core-#{System.unique_integer([:positive])}")
    File.mkdir_p!(base)

    config =
      Config.new(%{
        ipc_host: {127, 0, 0, 1},
        ipc_port: 49_000 + rem(System.unique_integer([:positive]), 500),
        lock_path: Path.join(base, "oneshot.lock"),
        temp_dir: Path.join(base, "Temp"),
        save_dir: Path.join(base, "Screenshots"),
        startup_key: "HKCU\\Software\\Microsoft\\Windows\\CurrentVersion\\Run",
        app_name: "OneShot"
      })

    on_exit(fn -> File.rm_rf(base) end)
    %{config: config}
  end

  test "acquire detects already running instance when server is reachable", %{config: config} do
    {:ok, file} = InstanceLock.acquire(config)

    {:ok, _pid} =
      start_supervised(
        {OneshotCore.Ipc.Server, [name: nil, config: config, handler: fn _ -> :ok end]}
      )

    assert {:error, :already_running} = InstanceLock.acquire(config)

    File.close(file)
    File.rm(config.lock_path)
  end

  test "acquire recovers from stale lock files", %{config: config} do
    File.mkdir_p!(Path.dirname(config.lock_path))
    File.write!(config.lock_path, ~s({"port":0,"created_at":"2026-03-20T00:00:00Z"}))

    assert {:ok, file} = InstanceLock.acquire(config)

    File.close(file)
    File.rm(config.lock_path)
  end
end
