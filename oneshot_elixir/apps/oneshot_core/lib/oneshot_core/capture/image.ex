defmodule OneshotCore.Capture.Image do
  @moduledoc false

  @enforce_keys [:png, :width, :height, :captured_at]
  defstruct [:png, :width, :height, :captured_at]

  @type t :: %__MODULE__{
          png: binary(),
          width: pos_integer(),
          height: pos_integer(),
          captured_at: DateTime.t()
        }
end
