defmodule OneshotCore.InstanceLock do
  @moduledoc false

  use GenServer

  def start_link(config) do
    GenServer.start_link(__MODULE__, config, name: __MODULE__)
  end

  def peek_primary(config \\ OneshotCore.Config.fetch()) do
    case acquire(config) do
      {:ok, file} ->
        File.close(file)
        File.rm(config.lock_path)
        :primary

      {:error, :already_running} ->
        :secondary
    end
  end

  def acquire(config \\ OneshotCore.Config.fetch()) do
    ensure_lock_dir!(config.lock_path)

    case File.open(config.lock_path, [:write, :exclusive, :utf8]) do
      {:ok, file} ->
        payload =
          Jason.encode!(%{
            "port" => config.ipc_port,
            "created_at" => DateTime.utc_now() |> DateTime.to_iso8601()
          })

        :ok = IO.binwrite(file, payload)
        {:ok, file}

      {:error, :eexist} ->
        if OneshotCore.Ipc.Client.ping(config: config, timeout: 250) == :ok do
          {:error, :already_running}
        else
          File.rm(config.lock_path)
          acquire(config)
        end

      {:error, reason} ->
        {:error, reason}
    end
  end

  @impl true
  def init(config) do
    case acquire(config) do
      {:ok, file} ->
        {:ok, %{config: config, file: file}}

      {:error, :already_running} ->
        {:stop, :already_running}

      {:error, reason} ->
        {:stop, reason}
    end
  end

  @impl true
  def terminate(_reason, %{config: config, file: file}) do
    File.close(file)
    File.rm(config.lock_path)
    :ok
  end

  def terminate(_reason, _state), do: :ok

  defp ensure_lock_dir!(lock_path) do
    lock_path
    |> Path.dirname()
    |> File.mkdir_p!()
  end
end
