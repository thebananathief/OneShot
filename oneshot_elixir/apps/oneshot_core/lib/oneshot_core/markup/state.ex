defmodule OneshotCore.Markup.State do
  @moduledoc false

  @enforce_keys [:tool, :primitives]
  defstruct [:tool, :primitives]

  @type t :: %__MODULE__{
          tool: atom(),
          primitives: [struct()]
        }
end
