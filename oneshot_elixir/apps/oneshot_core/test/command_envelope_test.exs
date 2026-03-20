defmodule OneshotCore.CommandEnvelopeTest do
  use ExUnit.Case, async: true

  alias OneshotCore.CommandEnvelope

  test "round-trips valid envelope payloads" do
    envelope = CommandEnvelope.new("snapshot")

    assert {:ok, decoded} =
             envelope
             |> CommandEnvelope.encode()
             |> IO.iodata_to_binary()
             |> CommandEnvelope.decode()

    assert decoded.command == "snapshot"
    assert decoded.request_id == envelope.request_id
  end

  test "rejects invalid commands" do
    payload =
      Jason.encode!(%{
        "v" => 1,
        "command" => "not-a-command",
        "request_id" => "req-1",
        "sent_at" => DateTime.utc_now() |> DateTime.to_iso8601()
      })

    assert {:error, :unsupported_command} = CommandEnvelope.decode(payload)
  end
end
