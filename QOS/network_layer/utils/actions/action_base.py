from abc import ABC, abstractmethod

# ----------------------------
# Action Classes
# ----------------------------

class Action(ABC):
    """Abstract base class for actions on a port."""
    @abstractmethod
    def install(self, port):
        """Set up the action on a given port."""
        pass

    @abstractmethod
    def apply_on_port(self, port):
        """Apply the action to a specific port."""
        pass

    def apply_on_all_ports(self, ports):
        """Apply the action to all provided ports."""
        for port in ports:
            self.apply_on_port(port)

    @abstractmethod
    def update_settings(self, **settings):
        """Update the parameters/settings of the action."""
        pass