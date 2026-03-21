defmodule OneshotCore.Notification.Item do
  @moduledoc false

  alias OneshotCore.Capture.Image
  alias OneshotCore.Markup.State

  @enforce_keys [:id, :image]
  defstruct [:id, :image, :temp_file, :markup_state]

  @type t :: %__MODULE__{
          id: String.t(),
          image: Image.t(),
          temp_file: String.t() | nil,
          markup_state: State.t() | nil
        }
end
