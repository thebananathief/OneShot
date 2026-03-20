defmodule OneshotCore.Ipc.Client do
  @moduledoc false

  alias OneshotCore.CommandEnvelope
  alias OneshotCore.Config

  def send_command(command, opts \\ []) when is_binary(command) do
    config = config_from(opts)
    timeout = Keyword.get(opts, :timeout, 1_500)

    with {:ok, socket} <- connect(config, timeout),
         :ok <-
           :gen_tcp.send(socket, [CommandEnvelope.new(command) |> CommandEnvelope.encode(), "\n"]),
         {:ok, response} <- :gen_tcp.recv(socket, 0, timeout),
         {:ok, decoded} <- Jason.decode(String.trim(response)) do
      :gen_tcp.close(socket)

      case decoded do
        %{"status" => "ok"} -> :ok
        %{"status" => "error", "reason" => reason} -> {:error, reason}
        _ -> {:error, :invalid_response}
      end
    else
      {:error, reason} -> {:error, reason}
    end
  end

  def ping(opts \\ []) do
    case send_command("ping", opts) do
      :ok -> :ok
      {:error, reason} -> {:error, reason}
    end
  end

  defp connect(config, timeout) do
    :gen_tcp.connect(
      config.ipc_host,
      config.ipc_port,
      [:binary, active: false, packet: :line],
      timeout
    )
  end

  defp config_from(opts) do
    case Keyword.get(opts, :config) do
      %Config{} = config -> config
      nil -> Config.fetch()
    end
  end
end
