defmodule OneshotCore.Config do
  @moduledoc false

  @enforce_keys [:ipc_host, :ipc_port, :lock_path, :temp_dir, :save_dir, :startup_key, :app_name]
  defstruct [:ipc_host, :ipc_port, :lock_path, :temp_dir, :save_dir, :startup_key, :app_name]

  @type t :: %__MODULE__{
          ipc_host: :inet.ip_address(),
          ipc_port: pos_integer(),
          lock_path: String.t(),
          temp_dir: String.t(),
          save_dir: String.t(),
          startup_key: String.t(),
          app_name: String.t()
        }

  @spec fetch() :: t()
  def fetch do
    :oneshot_core
    |> Application.get_all_env()
    |> Enum.into(%{})
    |> new()
  end

  @spec new(map() | keyword()) :: t()
  def new(attrs) when is_list(attrs) do
    attrs
    |> Enum.into(%{})
    |> new()
  end

  def new(attrs) when is_map(attrs) do
    struct!(__MODULE__, attrs)
  end
end
