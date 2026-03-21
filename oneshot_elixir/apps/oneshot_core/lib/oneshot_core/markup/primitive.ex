defmodule OneshotCore.Markup.Primitive do
  @moduledoc false

  defmodule Pen do
    @moduledoc false
    @enforce_keys [:points, :stroke_color, :stroke_thickness]
    defstruct [:points, :stroke_color, :stroke_thickness]
  end

  defmodule Line do
    @moduledoc false
    @enforce_keys [:start, :stop, :stroke_color, :stroke_thickness]
    defstruct [:start, :stop, :stroke_color, :stroke_thickness]
  end

  defmodule Arrow do
    @moduledoc false
    @enforce_keys [:start, :stop, :stroke_color, :stroke_thickness]
    defstruct [:start, :stop, :stroke_color, :stroke_thickness]
  end

  defmodule Rectangle do
    @moduledoc false
    @enforce_keys [:bounds, :stroke_color, :stroke_thickness]
    defstruct [:bounds, :stroke_color, :stroke_thickness, :fill_color]
  end

  defmodule Ellipse do
    @moduledoc false
    @enforce_keys [:bounds, :stroke_color, :stroke_thickness]
    defstruct [:bounds, :stroke_color, :stroke_thickness, :fill_color]
  end

  defmodule Polygon do
    @moduledoc false
    @enforce_keys [:points, :stroke_color, :stroke_thickness]
    defstruct [:points, :stroke_color, :stroke_thickness, :fill_color]
  end

  defmodule Text do
    @moduledoc false
    @enforce_keys [:text, :position, :font_family_name, :font_size, :font_color]
    defstruct [:text, :position, :font_family_name, :font_size, :font_color]
  end
end
