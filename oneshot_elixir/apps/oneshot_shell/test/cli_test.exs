defmodule OneshotShell.CLITest do
  use ExUnit.Case, async: true

  test "module loads" do
    assert Code.ensure_loaded?(OneshotShell.CLI)
    assert function_exported?(OneshotShell.CLI, :main, 1)
  end
end
