defmodule OneshotCore.Ipc.Server do
  @moduledoc false

  use GenServer

  alias OneshotCore.CommandEnvelope

  def start_link(opts) do
    case Keyword.get(opts, :name, __MODULE__) do
      nil -> GenServer.start_link(__MODULE__, opts)
      name -> GenServer.start_link(__MODULE__, opts, name: name)
    end
  end

  @impl true
  def init(opts) do
    config = Keyword.fetch!(opts, :config)
    handler = Keyword.fetch!(opts, :handler)

    {:ok, socket} =
      :gen_tcp.listen(config.ipc_port, [
        :binary,
        {:packet, :line},
        {:active, false},
        {:ip, config.ipc_host},
        {:reuseaddr, true}
      ])

    state = %{socket: socket, handler: handler, config: config}
    send(self(), :accept)
    {:ok, state}
  end

  @impl true
  def handle_info(:accept, state) do
    case :gen_tcp.accept(state.socket, 250) do
      {:ok, client} ->
        Task.start(fn -> serve_client(client, state.handler) end)
        send(self(), :accept)
        {:noreply, state}

      {:error, :timeout} ->
        send(self(), :accept)
        {:noreply, state}

      {:error, :closed} ->
        {:stop, :normal, state}

      {:error, reason} ->
        {:stop, reason, state}
    end
  end

  @impl true
  def terminate(_reason, %{socket: socket}) do
    :gen_tcp.close(socket)
    :ok
  end

  def terminate(_reason, _state), do: :ok

  defp serve_client(client, handler) do
    with {:ok, line} <- :gen_tcp.recv(client, 0, 1_500),
         {:ok, envelope} <- CommandEnvelope.decode(String.trim(line)),
         :ok <- handler.(envelope) do
      :ok = :gen_tcp.send(client, Jason.encode!(%{"status" => "ok"}) <> "\n")
    else
      {:error, reason} ->
        :gen_tcp.send(
          client,
          Jason.encode!(%{"status" => "error", "reason" => inspect(reason)}) <> "\n"
        )
    end

    :gen_tcp.close(client)
  end
end
