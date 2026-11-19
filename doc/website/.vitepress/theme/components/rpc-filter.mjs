export function filterCommands(commands, query, category, rootsOnly) {
  const words = query.toLowerCase().trim().split(/\s+/);
  return commands.filter(command => {
    if (category && command.category !== category) return false;
    if (rootsOnly && !command.change) return false;
    const text = [command.name, command.summary, command.description, command.change].join(" ").toLowerCase();
    return words.every(word => text.includes(word));
  });
}
