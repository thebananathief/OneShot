defmodule OneshotCore.IpcClientServerTest do
  use ExUnit.Case, async: false

  alias OneshotCore.Config
  alias OneshotCore.Ipc.Client
  alias OneshotCore.Ipc.Server

  setup do
    base = Path.join(System.tmp_dir!(), "oneshot-ipc-#{System.unique_integer([:positive])}")
    File.mkdir_p!(base)
    test_pid = self()

    config =
      Config.new(%{
        ipc_host: {127, 0, 0, 1},
        ipc_port: 50_000 + rem(System.unique_integer([:positive]), 500),
        lock_path: Path.join(base, "oneshot.lock"),
        temp_dir: Path.join(base, "Temp"),
        save_dir: Path.join(base, "Screenshots"),
        startup_key: "HKCU\\Software\\Microsoft\\Windows\\CurrentVersion\\Run",
        app_name: "OneShot"
      })

    handler = fn envelope ->
      send(test_pid, {:command, envelope.command})
      :ok
    end

    {:ok, _pid} =
      start_supervised({Server, [name: nil, config: config, handler: handler]})

    on_exit(fn -> File.rm_rf(base) end)
    %{config: config}
  end

  test "client delivers snapshot commands", %{config: config} do
    assert :ok = Client.send_command("snapshot", config: config)
    assert_receive {:command, "snapshot"}, 1_000
  end

  test "server ignores invalid payloads and continues serving", %{config: config} do
    {:ok, socket} =
      :gen_tcp.connect(
        config.ipc_host,
        config.ipc_port,
        [:binary, active: false, packet: :line],
        1_000
      )

    :ok =
      :gen_tcp.send(
        socket,
        "{\"v\":1,\"command\":\"invalid\",\"request_id\":\"req-1\",\"sent_at\":\"2026-03-20T00:00:00Z\"}\n"
      )

    assert {:ok, response} = :gen_tcp.recv(socket, 0, 1_000)
    assert response =~ "\"status\":\"error\""
    :gen_tcp.close(socket)

    assert :ok = Client.send_command("snapshot", config: config)
    assert_receive {:command, "snapshot"}, 1_000
  end
end
