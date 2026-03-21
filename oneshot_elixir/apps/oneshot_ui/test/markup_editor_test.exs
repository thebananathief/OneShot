defmodule OneshotUi.MarkupEditorTest do
  use ExUnit.Case, async: true

  test "result struct carries bitmap and saved path" do
    result = %OneshotUi.MarkupEditor.Result{bitmap: :bitmap_ref, saved_path: "C:/tmp/out.png"}
    assert result.bitmap == :bitmap_ref
    assert result.saved_path == "C:/tmp/out.png"
  end
end
