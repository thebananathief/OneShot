defmodule OneshotCore.Events do
  @moduledoc false

  @registry OneshotCore.EventsRegistry

  def subscribe(topic) do
    Registry.register(@registry, topic, [])
    :ok
  end

  def unsubscribe(topic) do
    Registry.unregister(@registry, topic)
    :ok
  end

  def broadcast(topic, payload) do
    Registry.dispatch(@registry, topic, fn entries ->
      Enum.each(entries, fn {pid, _value} ->
        send(pid, {:oneshot_event, topic, payload})
      end)
    end)

    :ok
  end
end
