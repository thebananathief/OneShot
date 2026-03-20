defmodule OneshotCore.CommandEnvelope do
  @moduledoc false

  @enforce_keys [:command, :request_id, :sent_at]
  defstruct version: 1, command: nil, request_id: nil, sent_at: nil

  @type t :: %__MODULE__{
          version: pos_integer(),
          command: String.t(),
          request_id: String.t(),
          sent_at: DateTime.t()
        }

  @allowed_commands ~w(snapshot install-startup uninstall-startup diagnostics ping)

  @spec new(String.t()) :: t()
  def new(command) when is_binary(command) do
    %__MODULE__{
      command: command,
      request_id: build_request_id(),
      sent_at: DateTime.utc_now() |> DateTime.truncate(:second)
    }
  end

  @spec encode(t()) :: iodata()
  def encode(%__MODULE__{} = envelope) do
    Jason.encode_to_iodata!(%{
      "v" => envelope.version,
      "command" => envelope.command,
      "request_id" => envelope.request_id,
      "sent_at" => DateTime.to_iso8601(envelope.sent_at)
    })
  end

  @spec decode(binary()) :: {:ok, t()} | {:error, term()}
  def decode(payload) when is_binary(payload) do
    with {:ok, decoded} <- Jason.decode(payload),
         {:ok, envelope} <- from_map(decoded) do
      {:ok, envelope}
    end
  end

  @spec from_map(map()) :: {:ok, t()} | {:error, term()}
  def from_map(%{
        "v" => 1,
        "command" => command,
        "request_id" => request_id,
        "sent_at" => sent_at
      })
      when is_binary(command) and is_binary(request_id) and is_binary(sent_at) do
    with :ok <- validate_command(command),
         {:ok, sent_at_dt, _offset} <- DateTime.from_iso8601(sent_at) do
      {:ok,
       %__MODULE__{
         version: 1,
         command: command,
         request_id: request_id,
         sent_at: sent_at_dt
       }}
    else
      {:error, _} = error -> error
      :error -> {:error, :invalid_timestamp}
    end
  end

  def from_map(_), do: {:error, :invalid_envelope}

  defp validate_command(command) when command in @allowed_commands, do: :ok
  defp validate_command(_command), do: {:error, :unsupported_command}

  defp build_request_id do
    integer = System.unique_integer([:positive, :monotonic])
    "req-" <> Integer.to_string(integer, 36)
  end
end
