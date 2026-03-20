defmodule OneshotCore do
  @moduledoc """
  Public entrypoints for the resident daemon runtime.
  """

  alias OneshotCore.CommandEnvelope
  alias OneshotCore.Config

  @spec dispatch(CommandEnvelope.t()) :: :ok | {:error, term()}
  def dispatch(%CommandEnvelope{} = envelope) do
    OneshotCore.CaptureCoordinator.dispatch(envelope)
  end

  @spec config() :: Config.t()
  def config do
    Config.fetch()
  end
end
